// FBXLoader: Assimp-backed FBX importer with skinning/animation extraction
#include "FBXLoader.h"
#ifdef USE_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#endif
#include <vector>
#include <unordered_map>
#include <functional>
#include <cmath>
#include <cctype>
#include <fstream>
#include <cstdlib>
#include <algorithm>

// --- small helpers ---
static std::string normalizeFbxAssimpName(const std::string& name){
  // Strip Assimp FBX pivot suffixes like "_$AssimpFbx$_Rotation", "_$AssimpFbx$_Translation", etc.
  size_t p = name.find("_$AssimpFbx$_");
  if(p != std::string::npos) return name.substr(0, p);
  return name;
}

static bool readFileBytes(const std::string& path, std::vector<unsigned char>& out){
  std::ifstream ifs(path, std::ios::binary);
  if(!ifs) return false;
  ifs.seekg(0, std::ios::end);
  std::streampos len = ifs.tellg();
  if(len <= 0) return false;
  out.resize((size_t)len);
  ifs.seekg(0, std::ios::beg);
  if(!ifs.read((char*)out.data(), len)) return false;
  return true;
}

static std::string utf16_to_utf8(const unsigned char* data, size_t size, bool bigEndian){
  // very small UTF-16 decoder with surrogate support
  auto read16 = [&](size_t i)->uint16_t{
    if(i+1>=size) return 0;
    if(bigEndian) return (uint16_t(data[i])<<8) | uint16_t(data[i+1]);
    else return (uint16_t(data[i+1])<<8) | uint16_t(data[i]);
  };
  std::string out;
  for(size_t i=0;i+1<size;){
    uint16_t w1 = read16(i); i+=2;
    if(w1==0) { out.push_back('\0'); continue; }
    if(w1>=0xD800 && w1<=0xDBFF){
      if(i+1<size){
        uint16_t w2 = read16(i); i+=2;
        if(w2>=0xDC00 && w2<=0xDFFF){
          uint32_t cp = 0x10000 + (((uint32_t)(w1-0xD800))<<10) + (uint32_t)(w2-0xDC00);
          if(cp<=0x10FFFF){
            // encode UTF-8
            out.push_back(char(0xF0 | ((cp>>18)&0x07)));
            out.push_back(char(0x80 | ((cp>>12)&0x3F)));
            out.push_back(char(0x80 | ((cp>>6)&0x3F)));
            out.push_back(char(0x80 | (cp&0x3F)));
            continue;
          }
        }
      }
      // invalid surrogate, replace with '?'
      out.push_back('?');
      continue;
    }
    uint32_t cp = w1;
    if(cp < 0x80){ out.push_back(char(cp)); }
    else if(cp < 0x800){
      out.push_back(char(0xC0 | (cp>>6)));
      out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(char(0xE0 | (cp>>12)));
      out.push_back(char(0x80 | ((cp>>6) & 0x3F)));
      out.push_back(char(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

static std::string detect_fbx_format(const std::vector<unsigned char>& bytes){
  // FBX Binary magic: "Kaydara FBX Binary  \x00\x1a\x00" then 4-byte LE version
  const char* magic = "Kaydara FBX Binary  ";
  if(bytes.size() >= 27 && std::equal(magic, magic+20, (const char*)bytes.data())){
    // version at offset 23..26 (after 20 + 3 sentinel bytes)
    unsigned int ver = 0;
    if(bytes.size() >= 27){
      ver = (unsigned int)bytes[23] | ((unsigned int)bytes[24]<<8) | ((unsigned int)bytes[25]<<16) | ((unsigned int)bytes[26]<<24);
    }
    return std::string("Binary v") + std::to_string(ver);
  }
  // Check UTF-16 BOM
  if(bytes.size()>=2){
    if(bytes[0]==0xFF && bytes[1]==0xFE) return "ASCII UTF-16LE";
    if(bytes[0]==0xFE && bytes[1]==0xFF) return "ASCII UTF-16BE";
  }
  // Heuristic for ASCII UTF-8
  std::string head;
  size_t N = std::min<size_t>(bytes.size(), 64);
  for(size_t i=0;i<N;++i){ unsigned char c = bytes[i]; if(c==0) break; head.push_back((char)( (c>=32 && c<127) ? c : '.') ); }
  if(head.find("FBXHeaderExtension") != std::string::npos || head.find("Objects:") != std::string::npos || head.find("Vertices:") != std::string::npos)
    return "ASCII UTF-8";
  return "Unknown";
}

static bool parseFBXAsciiGeometry(const std::string& txt, MeshData& out, std::string& reason){
  auto findSection = [&](const char* key)->std::pair<size_t,size_t>{
    size_t k = txt.find(key); if(k==std::string::npos) return {std::string::npos,std::string::npos};
    size_t b = txt.find('{', k); if(b==std::string::npos) return {k,std::string::npos};
    size_t a = txt.find("a:", b); if(a==std::string::npos) return {k,std::string::npos};
    size_t e = txt.find('}', a); if(e==std::string::npos) return {k,std::string::npos};
    return {a+2, e};
  };
  auto secV = findSection("Vertices:");
  auto secI = findSection("PolygonVertexIndex:");
  if(secV.first==std::string::npos || secI.first==std::string::npos){ reason = "缺少 Vertices 或 PolygonVertexIndex 段"; return false; }

  auto parseFloats = [&](size_t s, size_t e, std::vector<double>& vals){
    const char* p = txt.c_str()+s; const char* end = txt.c_str()+e; char* q=nullptr; vals.clear();
    while(p<end){
      // 跳过常见分隔符
      while(p<end && (*p==' '||*p=='\t'||*p=='\r'||*p=='\n'||*p==',')) ++p;
      if(p>=end) break;
      double v = std::strtod(p, &q);
      if(q==p){
        // 非数字字符，向前推进继续尝试（容忍注释/未知 token）
        ++p; continue;
      }
      vals.push_back(v);
      p = q;
    }
  };
  auto parseInts = [&](size_t s, size_t e, std::vector<int>& vals){
    const char* p = txt.c_str()+s; const char* end = txt.c_str()+e; char* q=nullptr; vals.clear();
    while(p<end){
      while(p<end && (*p==' '||*p=='\t'||*p=='\r'||*p=='\n'||*p==',')) ++p;
      if(p>=end) break;
      long v = std::strtol(p, &q, 10);
      if(q==p){ ++p; continue; }
      vals.push_back((int)v);
      p = q;
    }
  };

  std::vector<double> vf; parseFloats(secV.first, secV.second, vf);
  if(vf.size() < 9 || (vf.size()%3)!=0){ reason = "Vertices 数组无效"; return false; }
  std::vector<int> vi; parseInts(secI.first, secI.second, vi);
  if(vi.size() < 3){ reason = "PolygonVertexIndex 数组无效"; return false; }

  size_t vcount = vf.size()/3;
  std::vector<unsigned int> indices;
  indices.reserve(vi.size()*3/2);
  std::vector<unsigned int> poly;
  poly.reserve(8);
  auto flushPoly = [&](){
    if(poly.size()>=3){
      unsigned int v0 = poly[0];
      for(size_t i=1;i+1<poly.size();++i){ indices.push_back(v0); indices.push_back(poly[i]); indices.push_back(poly[i+1]); }
    }
    poly.clear();
  };
  for(int raw : vi){
    bool end = (raw < 0);
    unsigned int idx = end ? (unsigned)(-raw - 1) : (unsigned)raw;
    if(idx < vcount) poly.push_back(idx); else { poly.clear(); }
    if(end) flushPoly();
  }
  if(indices.empty()){ reason = "三角化后无索引"; return false; }

  // 生成顶点数据和法线
  out.vertices.clear(); out.indices.clear(); out.bones.clear(); out.channels.clear(); out.skinned=false; out.animDuration=0.0; out.animTicksPerSecond=25.0;
  std::vector<float> pos(vf.size()); for(size_t i=0;i<vf.size();++i) pos[i]=(float)vf[i];
  std::vector<float> norms(vcount*3, 0.0f);
  for(size_t i=0;i<indices.size(); i+=3){ unsigned int i0=indices[i], i1=indices[i+1], i2=indices[i+2]; if(i0>=vcount||i1>=vcount||i2>=vcount) continue; 
    float x0=pos[i0*3+0], y0=pos[i0*3+1], z0=pos[i0*3+2];
    float x1=pos[i1*3+0], y1=pos[i1*3+1], z1=pos[i1*3+2];
    float x2=pos[i2*3+0], y2=pos[i2*3+1], z2=pos[i2*3+2];
    float ux=x1-x0, uy=y1-y0, uz=z1-z0; float vx=x2-x0, vy=y2-y0, vz=z2-z0;
    float nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
    norms[i0*3+0]+=nx; norms[i0*3+1]+=ny; norms[i0*3+2]+=nz;
    norms[i1*3+0]+=nx; norms[i1*3+1]+=ny; norms[i1*3+2]+=nz;
    norms[i2*3+0]+=nx; norms[i2*3+1]+=ny; norms[i2*3+2]+=nz;
  }
  out.vertices.reserve(vcount*16);
  for(size_t i=0;i<vcount;++i){
    float x=pos[i*3+0], y=pos[i*3+1], z=pos[i*3+2];
    float nx=norms[i*3+0], ny=norms[i*3+1], nz=norms[i*3+2]; float len=std::sqrt(nx*nx+ny*ny+nz*nz); if(len>1e-8f){ nx/=len; ny/=len; nz/=len; } else { nx=0; ny=1; nz=0; }
    out.vertices.push_back(x); out.vertices.push_back(y); out.vertices.push_back(z);
    out.vertices.push_back(nx); out.vertices.push_back(ny); out.vertices.push_back(nz);
    out.vertices.push_back(0.0f); out.vertices.push_back(0.0f); // uv
    // bones/weights
    out.vertices.push_back(0.0f); out.vertices.push_back(0.0f); out.vertices.push_back(0.0f); out.vertices.push_back(0.0f);
    out.vertices.push_back(0.0f); out.vertices.push_back(0.0f); out.vertices.push_back(0.0f); out.vertices.push_back(0.0f);
  }
  out.indices = std::move(indices);
  reason = "OK";
  return true;
}

static std::string toLowerExt(const std::string& s){
  size_t dot = s.find_last_of('.');
  if(dot==std::string::npos) return "";
  std::string ext = s.substr(dot+1);
  for(char& c : ext) c = (char)std::tolower((unsigned char)c);
  return ext;
}

bool FBXLoader::load(const std::string& path, MeshData& out, std::string& log, bool forceBake, bool disableFallbacks){
#ifndef USE_ASSIMP
  (void)path; (void)out;
  log = "Assimp 未启用（编译时未定义 USE_ASSIMP）。请开启 CMake 选项 USE_ASSIMP。";
  return false;
#else
  out.vertices.clear();
  out.indices.clear();
  out.skinned = false;
  out.bones.clear();
  out.channels.clear();
  out.animDuration = 0.0;
  out.animTicksPerSecond = 25.0;

  Assimp::Importer importer;
  // 记录格式信息
  std::string ext = toLowerExt(path);
  if(ext.empty()) ext = "?";
  bool isFBX = (ext == "fbx");
  bool isGLTF = (ext == "gltf" || ext == "glb");
  unsigned int flags = 0
    | aiProcess_Triangulate
    | aiProcess_JoinIdenticalVertices
    | aiProcess_GenSmoothNormals
    | aiProcess_ImproveCacheLocality
    | aiProcess_RemoveRedundantMaterials
    | aiProcess_SortByPType
    | aiProcess_OptimizeMeshes
    | aiProcess_OptimizeGraph
    | aiProcess_CalcTangentSpace;

  // 仅对 FBX 设置 FBX 专属属性；其他格式（glTF 等）使用默认设置以避免副作用
  if(isFBX){
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ALL_GEOMETRY_LAYERS, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ALL_MATERIALS, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_MATERIALS, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_TEXTURES, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_CAMERAS, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_LIGHTS, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
  }
  // NOTE: Some Assimp versions don't define AI_CONFIG_IMPORT_FBX_STRICT_MODE. Skip for compatibility.

  const aiScene* scene = forceBake ? nullptr : importer.ReadFile(path, flags);
  if((!scene || !scene->mRootNode)){
    // Primary parse failed: try memory-based recovery (UTF-16 ASCII FBX etc.) then static fallback
    std::string err1 = importer.GetErrorString() ? importer.GetErrorString() : "unknown";
    // detect file format for better diagnostics
    std::vector<unsigned char> fileBytesDiag; std::string fmtInfo="";
    if(isFBX && readFileBytes(path, fileBytesDiag)) fmtInfo = detect_fbx_format(fileBytesDiag);

    // If user requested to skip fallbacks, fail fast here to avoid long stalls
    if(disableFallbacks){
      log = std::string("Direct import failed (fallbacks disabled): ") + err1 + (fmtInfo.empty()?"":std::string(" | format=")+fmtInfo);
      return false;
    }

    // 1) Try ReadFileFromMemory after possible UTF-16->UTF-8 conversion
    std::vector<unsigned char> fileBytes;
    if(readFileBytes(path, fileBytes)){
      bool isBE = false; bool isLE = false;
      if(fileBytes.size()>=2){
        if(fileBytes[0]==0xFF && fileBytes[1]==0xFE) isLE=true; // UTF-16 LE BOM
        else if(fileBytes[0]==0xFE && fileBytes[1]==0xFF) isBE=true; // UTF-16 BE BOM
      }
      // Heuristic: if many zeroes appear in either even or odd bytes, treat as UTF-16
      if(!isLE && !isBE && fileBytes.size()>512){
        size_t zeroEven=0, zeroOdd=0; size_t N = std::min<size_t>(fileBytes.size(), 4096);
        for(size_t i=0;i<N;i+=2){ if(fileBytes[i]==0) zeroEven++; if(i+1<N && fileBytes[i+1]==0) zeroOdd++; }
        if(zeroEven> N/8) isBE = true; // high zeros at even -> BE common for ASCII-range
        if(zeroOdd > N/8) isLE = true; // high zeros at odd -> LE common for ASCII-range
      }
      Assimp::Importer memImp;
      if(isFBX){
        memImp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ALL_GEOMETRY_LAYERS, true);
        memImp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ALL_MATERIALS, true);
        memImp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_MATERIALS, true);
        memImp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_TEXTURES, false);
        memImp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_CAMERAS, false);
        memImp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_LIGHTS, false);
        memImp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS, true);
        memImp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
      }
  // NOTE: Skip AI_CONFIG_IMPORT_FBX_STRICT_MODE for compatibility across Assimp versions.

  const aiScene* sceneMem = nullptr;
      if(isLE || isBE){
        // skip BOM if present
        const unsigned char* ptr = fileBytes.data(); size_t sz = fileBytes.size();
        size_t off = ( (isLE && sz>=2 && ptr[0]==0xFF && ptr[1]==0xFE) || (isBE && sz>=2 && ptr[0]==0xFE && ptr[1]==0xFF) ) ? 2 : 0;
        std::string utf8 = utf16_to_utf8(ptr+off, sz-off, isBE);
        const char* hint = isFBX ? "fbx" : (isGLTF ? (ext=="glb"?"glb":"gltf") : ext.c_str());
        sceneMem = memImp.ReadFileFromMemory(utf8.data(), utf8.size(), flags, hint);
      } else {
        // also try raw memory (covers some odd encodings)
        const char* hint = isFBX ? "fbx" : (isGLTF ? (ext=="glb"?"glb":"gltf") : ext.c_str());
        sceneMem = memImp.ReadFileFromMemory(fileBytes.data(), fileBytes.size(), flags, hint);
      }
  if(sceneMem && sceneMem->mRootNode && sceneMem->mNumMeshes>0){
        // Build from sceneMem (same as normal path)
        std::unordered_map<std::string, unsigned> boneIndexByName;
        auto getOrAddBoneIndex2 = [&](const std::string& name)->unsigned{
          auto it = boneIndexByName.find(name);
          if(it!=boneIndexByName.end()) return it->second;
          unsigned idx = (unsigned)out.bones.size(); MeshData::Bone b; b.name=name; b.parent=-1; for(int i=0;i<16;++i){ b.offset[i]=(i%5==0)?1.0f:0.0f; b.bindLocal[i]=(i%5==0)?1.0f:0.0f; } out.bones.push_back(b); boneIndexByName.emplace(name,idx); return idx;
        };
        auto copyMat2 = [](const aiMatrix4x4& m, float* dst){ dst[0]=m.a1; dst[4]=m.a2; dst[8]=m.a3; dst[12]=m.a4; dst[1]=m.b1; dst[5]=m.b2; dst[9]=m.b3; dst[13]=m.b4; dst[2]=m.c1; dst[6]=m.c2; dst[10]=m.c3; dst[14]=m.c4; dst[3]=m.d1; dst[7]=m.d2; dst[11]=m.d3; dst[15]=m.d4; };
        size_t vertexBase2 = 0;
        for(unsigned int m=0; m<sceneMem->mNumMeshes; ++m){
          const aiMesh* mesh = sceneMem->mMeshes[m]; if(!mesh || mesh->mPrimitiveTypes==aiPrimitiveType_LINE || mesh->mPrimitiveTypes==aiPrimitiveType_POINT) continue;
          bool hasNormals = (mesh->mNormals != nullptr); bool hasUV0 = mesh->HasTextureCoords(0);
          out.vertices.reserve(out.vertices.size() + mesh->mNumVertices * 16);
          for(unsigned int i=0;i<mesh->mNumVertices;++i){ aiVector3D p=mesh->mVertices[i]; aiVector3D n= hasNormals?mesh->mNormals[i]:aiVector3D(0,1,0); aiVector3D t= hasUV0?mesh->mTextureCoords[0][i]:aiVector3D(0,0,0);
            out.vertices.insert(out.vertices.end(), {p.x,p.y,p.z,n.x,n.y,n.z,t.x,t.y, 0,0,0,0, 0,0,0,0}); }
          for(unsigned int f=0; f<mesh->mNumFaces; ++f){ const aiFace& face=mesh->mFaces[f]; if(face.mNumIndices>=3){ for(unsigned int i=1;i+1<face.mNumIndices;++i){ out.indices.push_back((unsigned)(vertexBase2+face.mIndices[0])); out.indices.push_back((unsigned)(vertexBase2+face.mIndices[i])); out.indices.push_back((unsigned)(vertexBase2+face.mIndices[i+1])); } } }
          if(mesh->HasBones()){
            out.skinned = true;
            for(unsigned int b=0;b<mesh->mNumBones;++b){ const aiBone* bone=mesh->mBones[b]; if(!bone) continue; unsigned bi=getOrAddBoneIndex2(bone->mName.C_Str()); copyMat2(bone->mOffsetMatrix, out.bones[bi].offset); for(unsigned int w=0;w<bone->mNumWeights;++w){ const aiVertexWeight& vw=bone->mWeights[w]; unsigned int v=vw.mVertexId; float weight=vw.mWeight; size_t base=(vertexBase2+v)*16; int best=-1; float minW=1e9f; for(int k=0;k<4;++k){ float wgt=out.vertices[base+12+k]; if(wgt<=0.0f){ best=k; break; } if(wgt<minW){ minW=wgt; best=k; } } if(best>=0){ out.vertices[base+8+best]=(float)bi; out.vertices[base+12+best]=weight; } }
            }
            for(unsigned int v=0; v<mesh->mNumVertices; ++v){ size_t base=(vertexBase2+v)*16; float s=out.vertices[base+12]+out.vertices[base+13]+out.vertices[base+14]+out.vertices[base+15]; if(s>1e-6f){ out.vertices[base+12]/=s; out.vertices[base+13]/=s; out.vertices[base+14]/=s; out.vertices[base+15]/=s; } else { out.vertices[base+12]=out.vertices[base+13]=out.vertices[base+14]=out.vertices[base+15]=0.0f; } }
          }
          vertexBase2 += mesh->mNumVertices;
        }
        // Build local/parent maps for parent indices and bindLocal
        std::unordered_map<std::string, aiMatrix4x4> localByName;
        std::unordered_map<std::string, std::string> parentByName;
        if(sceneMem && sceneMem->mRootNode){
          std::function<void(const aiNode*, const aiNode*)> walk2 = [&](const aiNode* n, const aiNode* p){ if(!n) return; std::string nm = normalizeFbxAssimpName(n->mName.C_Str()); if(p){ parentByName[nm] = normalizeFbxAssimpName(p->mName.C_Str()); } localByName[nm] = n->mTransformation; for(unsigned int i=0;i<n->mNumChildren;++i) walk2(n->mChildren[i], n); };
          walk2(sceneMem->mRootNode, nullptr);
        }
        // Extract animation
        out.channels.clear();
        if(sceneMem->mNumAnimations > 0 && sceneMem->mAnimations[0]){
          aiAnimation* anim = sceneMem->mAnimations[0];
          out.animDuration=anim->mDuration;
          out.animTicksPerSecond=(anim->mTicksPerSecond>0.0)?anim->mTicksPerSecond:25.0;
          for(unsigned int c=0;c<anim->mNumChannels;++c){
            aiNodeAnim* ch=anim->mChannels[c]; if(!ch) continue;
            std::string chName = normalizeFbxAssimpName(ch->mNodeName.C_Str());
            auto it=boneIndexByName.find(chName);
            if(it==boneIndexByName.end()) continue;
            MeshData::Channel chan; chan.boneIndex=(int)it->second;
            for(unsigned int i=0;i<ch->mNumPositionKeys;++i){ MeshData::KeyVec3 kv{ ch->mPositionKeys[i].mTime, { (float)ch->mPositionKeys[i].mValue.x, (float)ch->mPositionKeys[i].mValue.y, (float)ch->mPositionKeys[i].mValue.z } }; chan.posKeys.push_back(kv);} 
            for(unsigned int i=0;i<ch->mNumRotationKeys;++i){ MeshData::KeyQuat kq{ ch->mRotationKeys[i].mTime, { (float)ch->mRotationKeys[i].mValue.x, (float)ch->mRotationKeys[i].mValue.y, (float)ch->mRotationKeys[i].mValue.z, (float)ch->mRotationKeys[i].mValue.w } }; chan.rotKeys.push_back(kq);} 
            for(unsigned int i=0;i<ch->mNumScalingKeys;++i){ MeshData::KeyVec3 ks{ ch->mScalingKeys[i].mTime, { (float)ch->mScalingKeys[i].mValue.x, (float)ch->mScalingKeys[i].mValue.y, (float)ch->mScalingKeys[i].mValue.z } }; chan.sclKeys.push_back(ks);} 
            out.channels.push_back(std::move(chan));
          }
        }
        // bindLocal and parent indices
        for(auto& b : out.bones){ auto itL = localByName.find(b.name); if(itL!=localByName.end()){ copyMat2(itL->second, b.bindLocal); } }
        for(size_t i=0;i<out.bones.size(); ++i){ auto itp=parentByName.find(out.bones[i].name); if(itp!=parentByName.end()){ auto itbi=boneIndexByName.find(itp->second); out.bones[i].parent=(itbi!=boneIndexByName.end())?(int)itbi->second:-1; } else out.bones[i].parent=-1; }
        if(!out.indices.empty()){ return true; }
      }
    }

    // 2) Static-geometry fallback with PreTransformVertices from file
    if(disableFallbacks){
      log = std::string("Import failed (fallbacks disabled): ") + err1 + (fmtInfo.empty()?"":std::string(" | format=")+fmtInfo);
      return false;
    }
  Assimp::Importer fallback;
    unsigned int flags2 = 0
      | aiProcess_Triangulate
      | aiProcess_JoinIdenticalVertices
      | aiProcess_GenSmoothNormals
      | aiProcess_ImproveCacheLocality
      | aiProcess_SortByPType
      | aiProcess_OptimizeMeshes
      | aiProcess_RemoveRedundantMaterials
      | aiProcess_PreTransformVertices; // 烘焙节点变换
    if(isFBX){
      fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ALL_GEOMETRY_LAYERS, true);
      fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ALL_MATERIALS, true);
      fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_MATERIALS, true);
      fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_TEXTURES, false);
      fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_CAMERAS, false);
      fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_LIGHTS, false);
      fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS, false);
      fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    }
  // NOTE: Skip AI_CONFIG_IMPORT_FBX_STRICT_MODE for compatibility across Assimp versions.
  const aiScene* scene2 = fallback.ReadFile(path, flags2);
    if(scene2 && scene2->mNumMeshes>0){
      
      size_t vertexBase = 0;
      for(unsigned int m=0; m<scene2->mNumMeshes; ++m){
        const aiMesh* mesh = scene2->mMeshes[m];
        if(!mesh) continue;
        bool hasNormals = (mesh->mNormals != nullptr);
        bool hasUV0 = (mesh->HasTextureCoords(0));
        out.vertices.reserve(out.vertices.size() + mesh->mNumVertices * 16);
        for(unsigned int i=0; i<mesh->mNumVertices; ++i){
          aiVector3D p = mesh->mVertices[i];
          aiVector3D n = hasNormals ? mesh->mNormals[i] : aiVector3D(0,1,0);
          aiVector3D t = hasUV0 ? mesh->mTextureCoords[0][i] : aiVector3D(0,0,0);
          out.vertices.push_back(p.x); out.vertices.push_back(p.y); out.vertices.push_back(p.z);
          out.vertices.push_back(n.x); out.vertices.push_back(n.y); out.vertices.push_back(n.z);
          out.vertices.push_back(t.x); out.vertices.push_back(t.y);
          // 无蒙皮
          out.vertices.push_back(0); out.vertices.push_back(0); out.vertices.push_back(0); out.vertices.push_back(0);
          out.vertices.push_back(0); out.vertices.push_back(0); out.vertices.push_back(0); out.vertices.push_back(0);
        }
        for(unsigned int f=0; f<mesh->mNumFaces; ++f){
          const aiFace& face = mesh->mFaces[f];
          if(face.mNumIndices >= 3){
            for(unsigned int i=1; i+1<face.mNumIndices; ++i){
              out.indices.push_back(static_cast<unsigned int>(vertexBase + face.mIndices[0]));
              out.indices.push_back(static_cast<unsigned int>(vertexBase + face.mIndices[i]));
              out.indices.push_back(static_cast<unsigned int>(vertexBase + face.mIndices[i+1]));
            }
          }
        }
        vertexBase += mesh->mNumVertices;
      }
      if(!out.indices.empty()){ out.skinned=false; out.bones.clear(); out.channels.clear(); out.animDuration=0.0; out.animTicksPerSecond=25.0; return true; }
    }
    std::string err2 = fallback.GetErrorString() ? fallback.GetErrorString() : "";
    // 3) Last resort: ASCII FBX manual parser (Vertices + PolygonVertexIndex)
  std::vector<unsigned char> bytes;
  if(!disableFallbacks && isFBX && readFileBytes(path, bytes)){
      bool isBE=false,isLE=false; if(bytes.size()>=2){ if(bytes[0]==0xFF&&bytes[1]==0xFE) isLE=true; else if(bytes[0]==0xFE&&bytes[1]==0xFF) isBE=true; }
      std::string txt;
      if(isLE||isBE){ txt = utf16_to_utf8(bytes.data()+2, bytes.size()-2, isBE); }
      else { txt.assign((const char*)bytes.data(), bytes.size()); }
      std::string why;
      if(parseFBXAsciiGeometry(txt, out, why)){ return true; }
    }
    log = isFBX
      ? std::string("FBX 读取失败：") + err1 + (err2.empty()?"":" | 回退失败："+err2) + (fmtInfo.empty()?"":std::string(" | format=")+fmtInfo) + " | 建议：另存为 ASCII FBX 7.4 或 glTF 2.0 再试"
      : std::string("Import failed: ") + (importer.GetErrorString()?importer.GetErrorString():"unknown") + (err2.empty()?"":" | fallback:"+err2);
    return false;
  }

  // Bone name mapping across meshes
  std::unordered_map<std::string, unsigned> boneIndexByName;
  auto getOrAddBoneIndex = [&](const std::string& name)->unsigned{
    std::string key = normalizeFbxAssimpName(name);
    auto it = boneIndexByName.find(key);
    if(it!=boneIndexByName.end()) return it->second;
    unsigned idx = (unsigned)out.bones.size();
    MeshData::Bone b; b.name = key; b.parent = -1; for(int i=0;i<16;++i){ b.offset[i] = (i%5==0)?1.0f:0.0f; b.bindLocal[i] = (i%5==0)?1.0f:0.0f; }
    out.bones.push_back(b);
    boneIndexByName.emplace(key, idx);
    return idx;
  };
  auto copyMat = [](const aiMatrix4x4& m, float* dst){
    dst[0]=m.a1; dst[4]=m.a2; dst[8]=m.a3;  dst[12]=m.a4;
    dst[1]=m.b1; dst[5]=m.b2; dst[9]=m.b3;  dst[13]=m.b4;
    dst[2]=m.c1; dst[6]=m.c2; dst[10]=m.c3; dst[14]=m.c4;
    dst[3]=m.d1; dst[7]=m.d2; dst[11]=m.d3; dst[15]=m.d4;
  };

  // Merge meshes
  size_t vertexBase = 0;
  for(unsigned int m=0; m<scene->mNumMeshes; ++m){
    const aiMesh* mesh = scene->mMeshes[m];
    if(!mesh || mesh->mPrimitiveTypes == aiPrimitiveType_LINE || mesh->mPrimitiveTypes == aiPrimitiveType_POINT) continue;

    bool hasNormals = (mesh->mNormals != nullptr);
    bool hasUV0 = (mesh->HasTextureCoords(0));

    out.vertices.reserve(out.vertices.size() + mesh->mNumVertices * 16);
    for(unsigned int i=0; i<mesh->mNumVertices; ++i){
      aiVector3D p = mesh->mVertices[i];
      aiVector3D n = hasNormals ? mesh->mNormals[i] : aiVector3D(0,1,0);
      aiVector3D t = hasUV0 ? mesh->mTextureCoords[0][i] : aiVector3D(0,0,0);
      out.vertices.push_back(p.x);
      out.vertices.push_back(p.y);
      out.vertices.push_back(p.z);
      out.vertices.push_back(n.x);
      out.vertices.push_back(n.y);
      out.vertices.push_back(n.z);
      out.vertices.push_back(t.x);
      out.vertices.push_back(t.y);
  // default bone attrs: indices=0, weights=0（无权重的顶点在 shader 中回退为未蒙皮）
  out.vertices.push_back(0.0f);
  out.vertices.push_back(0.0f);
  out.vertices.push_back(0.0f);
  out.vertices.push_back(0.0f);
  out.vertices.push_back(0.0f);
  out.vertices.push_back(0.0f);
  out.vertices.push_back(0.0f);
  out.vertices.push_back(0.0f);
    }
    for(unsigned int f=0; f<mesh->mNumFaces; ++f){
      const aiFace& face = mesh->mFaces[f];
      if(face.mNumIndices >= 3){
        // robust: fan-triangulate any ngon if Triangulate not applied
        for(unsigned int i=1; i+1<face.mNumIndices; ++i){
          out.indices.push_back(static_cast<unsigned int>(vertexBase + face.mIndices[0]));
          out.indices.push_back(static_cast<unsigned int>(vertexBase + face.mIndices[i]));
          out.indices.push_back(static_cast<unsigned int>(vertexBase + face.mIndices[i+1]));
        }
      }
    }
    if(mesh->HasBones()){
      out.skinned = true;
      for(unsigned int b=0; b<mesh->mNumBones; ++b){
        const aiBone* bone = mesh->mBones[b]; if(!bone) continue;
        unsigned bi = getOrAddBoneIndex(bone->mName.C_Str());
        copyMat(bone->mOffsetMatrix, out.bones[bi].offset);
        for(unsigned int w=0; w<bone->mNumWeights; ++w){
          const aiVertexWeight& vw = bone->mWeights[w];
          unsigned int v = vw.mVertexId; float weight = vw.mWeight;
          size_t base = (vertexBase + v) * 16;
          int bestSlot=-1; float minW=1e9f;
          for(int k=0;k<4;++k){ float wgt = out.vertices[base+12+k]; if(wgt<=0.0f){ bestSlot=k; break; } if(wgt<minW){ minW=wgt; bestSlot=k; } }
          if(bestSlot>=0){ out.vertices[base+8+bestSlot]=(float)bi; out.vertices[base+12+bestSlot]=weight; }
        }
      }
      for(unsigned int v=0; v<mesh->mNumVertices; ++v){
        size_t base = (vertexBase + v) * 16;
        float s = out.vertices[base+12]+out.vertices[base+13]+out.vertices[base+14]+out.vertices[base+15];
        if(s>1e-6f){
          out.vertices[base+12]/=s; out.vertices[base+13]/=s; out.vertices[base+14]/=s; out.vertices[base+15]/=s;
        } else {
          // 保持 0 权重，交由 shader 做未蒙皮回退
          out.vertices[base+12]=0.0f; out.vertices[base+13]=0.0f; out.vertices[base+14]=0.0f; out.vertices[base+15]=0.0f;
        }
      }
    }
    vertexBase += mesh->mNumVertices;
  }

  if(out.vertices.empty() || out.indices.empty()){
    if(disableFallbacks){
      log = "Assimp 直接读取未生成三角网格，且已禁用回退";
      return false;
    }
    // 首次未得到三角网格，尝试回退方案：预变换顶点（会丢失骨骼/动画）
    size_t meshCount = scene->mNumMeshes;
    size_t triFaces = 0;
    size_t totalVerts = 0;
    size_t boneMeshes = 0;
    // 统计节点总数，帮助诊断导入结果
    std::function<size_t(const aiNode*)> countNodes = [&](const aiNode* n)->size_t{
      if(!n) return 0; size_t c=1; for(unsigned int i=0;i<n->mNumChildren;++i) c+=countNodes(n->mChildren[i]); return c;
    };
    size_t nodeCount = countNodes(scene->mRootNode);
    for(unsigned int m=0; m<scene->mNumMeshes; ++m){
      const aiMesh* mesh = scene->mMeshes[m];
      if(!mesh) continue;
      totalVerts += mesh->mNumVertices;
      if(mesh->mNumBones>0) boneMeshes++;
      for(unsigned int f=0; f<mesh->mNumFaces; ++f){ const aiFace& face = mesh->mFaces[f]; if(face.mNumIndices==3) triFaces++; }
    }
    // 回退重载
    Assimp::Importer fallback;
    unsigned int flags2 = 0
      | aiProcess_Triangulate
      | aiProcess_JoinIdenticalVertices
      | aiProcess_GenSmoothNormals
      | aiProcess_ImproveCacheLocality
      | aiProcess_SortByPType
      | aiProcess_OptimizeMeshes
      | aiProcess_RemoveRedundantMaterials
      | aiProcess_PreTransformVertices; // 烘焙节点变换
    // 同步 FBX 属性
    fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ALL_GEOMETRY_LAYERS, true);
    fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ALL_MATERIALS, true);
    fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_MATERIALS, true);
    fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_TEXTURES, false);
    fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_CAMERAS, false);
    fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_LIGHTS, false);
    fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS, false);
    fallback.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const aiScene* scene2 = fallback.ReadFile(path, flags2);
    if(scene2 && scene2->mNumMeshes>0){
      out.vertices.clear(); out.indices.clear(); out.skinned=false; out.bones.clear(); out.channels.clear();
      vertexBase = 0;
      for(unsigned int m=0; m<scene2->mNumMeshes; ++m){
        const aiMesh* mesh = scene2->mMeshes[m];
        if(!mesh) continue;
        bool hasNormals = (mesh->mNormals != nullptr);
        bool hasUV0 = (mesh->HasTextureCoords(0));
        out.vertices.reserve(out.vertices.size() + mesh->mNumVertices * 16);
        for(unsigned int i=0; i<mesh->mNumVertices; ++i){
          aiVector3D p = mesh->mVertices[i];
          aiVector3D n = hasNormals ? mesh->mNormals[i] : aiVector3D(0,1,0);
          aiVector3D t = hasUV0 ? mesh->mTextureCoords[0][i] : aiVector3D(0,0,0);
          out.vertices.push_back(p.x); out.vertices.push_back(p.y); out.vertices.push_back(p.z);
          out.vertices.push_back(n.x); out.vertices.push_back(n.y); out.vertices.push_back(n.z);
          out.vertices.push_back(t.x); out.vertices.push_back(t.y);
          // 无蒙皮
          out.vertices.push_back(0); out.vertices.push_back(0); out.vertices.push_back(0); out.vertices.push_back(0);
          out.vertices.push_back(0); out.vertices.push_back(0); out.vertices.push_back(0); out.vertices.push_back(0);
        }
        for(unsigned int f=0; f<mesh->mNumFaces; ++f){
          const aiFace& face = mesh->mFaces[f];
          if(face.mNumIndices >= 3){
            for(unsigned int i=1; i+1<face.mNumIndices; ++i){
              out.indices.push_back(static_cast<unsigned int>(vertexBase + face.mIndices[0]));
              out.indices.push_back(static_cast<unsigned int>(vertexBase + face.mIndices[i]));
              out.indices.push_back(static_cast<unsigned int>(vertexBase + face.mIndices[i+1]));
            }
          }
        }
        vertexBase += mesh->mNumVertices;
      }
      if(!out.indices.empty()){
        log = "FBX 回退：已烘焙变换（动画/蒙皮不可用）";
        return true;
      }
    }
    // 回退失败，输出诊断
    std::string err1 = importer.GetErrorString() ? importer.GetErrorString() : "";
    std::string err2 = (scene2==nullptr) ? (fallback.GetErrorString() ? fallback.GetErrorString() : "") : "";
    log = "FBX 无三角网格: meshes=" + std::to_string(meshCount)
        + ", triFaces=" + std::to_string(triFaces)
        + ", totalVerts=" + std::to_string(totalVerts)
        + ", boneMeshes=" + std::to_string(boneMeshes)
        + ", nodes=" + std::to_string(nodeCount)
        + (err1.empty()?"":" | err=\""+err1+"\"")
        + (err2.empty()?"":" | fbErr=\""+err2+"\"");
    return false;
  }

  // Skeleton graph: build parent/local maps first
  std::unordered_map<std::string, std::string> parentByName;
  std::unordered_map<std::string, aiMatrix4x4> localByName;
  if(scene && scene->mRootNode){
    std::function<void(const aiNode*, const aiNode*)> walk;
    walk = [&](const aiNode* node, const aiNode* parent){
      if(!node) return;
      std::string childName = normalizeFbxAssimpName(node->mName.C_Str());
      if(parent){
        std::string parentName = normalizeFbxAssimpName(parent->mName.C_Str());
        parentByName[childName] = parentName;
      }
      localByName[childName] = node->mTransformation;
      for(unsigned int i=0;i<node->mNumChildren;++i) walk(node->mChildren[i], node);
    };
    walk(scene->mRootNode, nullptr);
  }

  

  // Extract animation after ancestors are included so channels for parents are kept
  out.channels.clear();
  if(scene->mNumAnimations > 0 && scene->mAnimations[0]){
    aiAnimation* anim = scene->mAnimations[0];
    out.animDuration = anim->mDuration;
    out.animTicksPerSecond = (anim->mTicksPerSecond > 0.0) ? anim->mTicksPerSecond : 25.0;
    for(unsigned int c=0;c<anim->mNumChannels;++c){
      aiNodeAnim* ch = anim->mChannels[c]; if(!ch) continue;
      std::string chName = normalizeFbxAssimpName(ch->mNodeName.C_Str());
      auto it = boneIndexByName.find(chName);
      if(it==boneIndexByName.end()) continue; // skip non-skeleton nodes
      MeshData::Channel chan; chan.boneIndex = (int)it->second;
      for(unsigned int i=0;i<ch->mNumPositionKeys;++i){ MeshData::KeyVec3 kv{ ch->mPositionKeys[i].mTime, { (float)ch->mPositionKeys[i].mValue.x, (float)ch->mPositionKeys[i].mValue.y, (float)ch->mPositionKeys[i].mValue.z } }; chan.posKeys.push_back(kv); }
      for(unsigned int i=0;i<ch->mNumRotationKeys;++i){ MeshData::KeyQuat kq{ ch->mRotationKeys[i].mTime, { (float)ch->mRotationKeys[i].mValue.x, (float)ch->mRotationKeys[i].mValue.y, (float)ch->mRotationKeys[i].mValue.z, (float)ch->mRotationKeys[i].mValue.w } }; chan.rotKeys.push_back(kq); }
      for(unsigned int i=0;i<ch->mNumScalingKeys;++i){ MeshData::KeyVec3 ks{ ch->mScalingKeys[i].mTime, { (float)ch->mScalingKeys[i].mValue.x, (float)ch->mScalingKeys[i].mValue.y, (float)ch->mScalingKeys[i].mValue.z } }; chan.sclKeys.push_back(ks); }
      out.channels.push_back(std::move(chan));
    }
  }

  // Parent indices and bindLocal
  if(!out.bones.empty()){
    for(size_t i=0;i<out.bones.size(); ++i){
      auto itp = parentByName.find(out.bones[i].name);
      if(itp!=parentByName.end()){
        auto itbi = boneIndexByName.find(itp->second);
        out.bones[i].parent = (itbi!=boneIndexByName.end()) ? (int)itbi->second : -1;
      }else{
        out.bones[i].parent = -1;
      }
      auto itL = localByName.find(out.bones[i].name);
      if(itL != localByName.end()){
        copyMat(itL->second, out.bones[i].bindLocal);
      }
    }
  }
  return true;
#endif
}
