#include "OBJLoader.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <tuple>
#include <cmath>

struct Key { int v, vt, vn; };
struct KeyHash { size_t operator()(const Key& k) const { return ((k.v*73856093) ^ (k.vt*19349663) ^ (k.vn*83492791)); } };
struct KeyEq { bool operator()(const Key& a, const Key& b) const { return a.v==b.v && a.vt==b.vt && a.vn==b.vn; } };

// 解析一个顶点索引三元组；允许负索引（OBJ 相对索引），返回是否成功
static bool parseIndexToken(const std::string& s, int& v, int& vt, int& vn){
  v = vt = vn = 0;
  try{
    // 形态：v | v/vt | v//vn | v/vt/vn
    size_t p1 = s.find('/');
    if(p1==std::string::npos){ v = std::stoi(s); return true; }
    size_t p2 = s.find('/', p1+1);
    v = std::stoi(s.substr(0,p1));
    if(p2==std::string::npos){ vt = std::stoi(s.substr(p1+1)); return true; }
    if(p2==p1+1){ vt = 0; vn = std::stoi(s.substr(p2+1)); return true; }
    vt = std::stoi(s.substr(p1+1, p2-p1-1));
    vn = std::stoi(s.substr(p2+1));
    return true;
  }catch(...){
    return false;
  }
}

bool OBJLoader::load(const std::string& path, MeshData& out, std::string& log){
  std::ifstream f(path);
  if(!f){ log = "Failed to open OBJ: "+path; return false; }

  std::vector<std::array<float,3>> pos;
  std::vector<std::array<float,3>> nor;
  std::vector<std::array<float,2>> uv;

  // 清空输出并重置蒙皮/动画字段
  out.vertices.clear();
  out.indices.clear();
  out.skinned = false;
  out.bones.clear();
  out.channels.clear();
  out.animDuration = 0.0;
  out.animTicksPerSecond = 25.0;

  std::unordered_map<Key, unsigned, KeyHash, KeyEq> map;
  std::vector<float> verts;
  std::vector<unsigned> idx;
  bool missingNormals = false;

  std::string line;
  while(std::getline(f, line)){
    if(line.empty() || line[0]=='#') continue;
    std::istringstream ss(line);
    std::string tag; ss >> tag;
    if(tag=="v"){
      float x,y,z; ss>>x>>y>>z; pos.push_back({x,y,z});
    }else if(tag=="vn"){
      float x,y,z; ss>>x>>y>>z; nor.push_back({x,y,z});
    }else if(tag=="vt"){
      float u,vv; ss>>u>>vv; uv.push_back({u,vv});
    }else if(tag=="f"){
      // 收集该面的全部顶点 token
      std::vector<std::string> tokens;
      std::string tok;
      while(ss >> tok){ if(!tok.empty()) tokens.push_back(tok); }
      if(tokens.size() < 3){ log = "Face has fewer than 3 vertices"; return false; }

      // 先将 tokens 转为索引三元组，处理负索引为正向（1-based）
      struct Trio { int v, vt, vn; };
      std::vector<Trio> triolist; triolist.reserve(tokens.size());
      for(const auto& sTok : tokens){
        int iv=0, it=0, in=0;
        if(!parseIndexToken(sTok, iv, it, in)){
          log = "Invalid face index token: "+sTok; return false;
        }
        // 负索引支持：负数表示相对于当前列表尾部
        if(iv<0) iv = (int)pos.size() + iv + 1;
        if(it<0) it = (int)uv.size()  + it + 1;
        if(in<0) in = (int)nor.size() + in + 1;
        // 允许 vt/vn 为 0（缺失），但 v 必须有效
        if(iv <= 0 || iv > (int)pos.size()){
          log = "Position index out of range"; return false;
        }
        if(it < 0 || it > (int)uv.size()){ log = "Texcoord index out of range"; return false; }
        if(in < 0 || in > (int)nor.size()){ log = "Normal index out of range"; return false; }
        triolist.push_back({iv,it,in});
      }

      auto emitVertex = [&](const Trio& tr){
        Key k{tr.v, tr.vt, tr.vn};
        auto it = map.find(k);
        if(it==map.end()){
          const auto& p = pos.at(k.v-1);
          std::array<float,3> n = {0,0,0};
          std::array<float,2> t = {0,0};
          if(k.vn>0){ n = nor.at(k.vn-1); }
          else { missingNormals = true; }
          if(k.vt>0){ t = uv.at(k.vt-1); }
          unsigned base = (unsigned)verts.size()/16;
          // 统一布局: pos(3), normal(3), uv(2), boneIdx(4f), boneW(4)
          verts.push_back(p[0]);
          verts.push_back(p[1]);
          verts.push_back(p[2]);
          verts.push_back(n[0]);
          verts.push_back(n[1]);
          verts.push_back(n[2]);
          verts.push_back(t[0]);
          verts.push_back(t[1]);
          // indices (0,0,0,0)
          verts.push_back(0.0f);
          verts.push_back(0.0f);
          verts.push_back(0.0f);
          verts.push_back(0.0f);
          // weights (1,0,0,0)
          verts.push_back(1.0f);
          verts.push_back(0.0f);
          verts.push_back(0.0f);
          verts.push_back(0.0f);
          map.emplace(k, base);
          idx.push_back(base);
        }else{
          idx.push_back(it->second);
        }
      };

      for(size_t i=1;i+1<triolist.size();++i){
        emitVertex(triolist[0]);
        emitVertex(triolist[i]);
        emitVertex(triolist[i+1]);
      }
    }
  }

  out.vertices = std::move(verts);
  out.indices = std::move(idx);
  if(out.indices.empty()){ log = "No faces parsed"; return false; }

  // 如果 OBJ 中缺失法线，则基于几何计算平滑法线
  if(missingNormals){
    const size_t vertCount = out.vertices.size() / 16;
    std::vector<std::array<float,3>> acc(vertCount, {0.f,0.f,0.f});
    auto normalize = [](std::array<float,3>& v){
      float len = std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
      if(len>1e-6f){ v[0]/=len; v[1]/=len; v[2]/=len; }
      else { v = {0.f,0.f,1.f}; }
    };
    for(size_t i=0;i+2<out.indices.size(); i+=3){
      unsigned i0 = out.indices[i];
      unsigned i1 = out.indices[i+1];
      unsigned i2 = out.indices[i+2];
      float* v0 = &out.vertices[i0*16];
      float* v1 = &out.vertices[i1*16];
      float* v2 = &out.vertices[i2*16];
      // 位置
      float e1x = v1[0]-v0[0], e1y = v1[1]-v0[1], e1z = v1[2]-v0[2];
      float e2x = v2[0]-v0[0], e2y = v2[1]-v0[1], e2z = v2[2]-v0[2];
      // 面法线 n = e1 x e2
      std::array<float,3> n = { e1y*e2z - e1z*e2y, e1z*e2x - e1x*e2z, e1x*e2y - e1y*e2x };
      normalize(n);
      for(unsigned vi : {i0,i1,i2}){
        acc[vi][0] += n[0];
        acc[vi][1] += n[1];
        acc[vi][2] += n[2];
      }
    }
    for(size_t i=0;i<vertCount;++i){
      auto n = acc[i];
      normalize(n);
      float* dst = &out.vertices[i*16];
      dst[3] = n[0]; dst[4] = n[1]; dst[5] = n[2];
    }
  }
  return true;
}
