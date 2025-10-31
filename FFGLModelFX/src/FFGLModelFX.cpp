#include "FFGLModelFX.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <atomic>
#include <thread>
#include <dlfcn.h>
#include <FFGLSDK.h>
#include <limits>
#include <cstring>
#include "FBXLoader.h"

using std::string;

static std::string DefaultModelPath(){
#ifdef __APPLE__
  const char* home = getenv("HOME");
  if(home) return std::string(home) + "/Documents/FFGLModelFX/model.obj";
#endif
  return "model.obj";
}

bool ModelRenderer::init(const std::string& shaderDir, std::string& log){
  std::string vs = shaderDir + "/model.vert";
  std::string fs = shaderDir + "/model.frag";
  if(!shader_.loadFromFiles(vs, fs, log)) return false;
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);
  // 记录启动时间用于连续自转（恢复）
  startTime_ = std::chrono::steady_clock::now();
  return true;
}

std::string ModelRenderer::getAnimStatsString() const{
  char buf[256];
  int keptBones = (int)bones_.size();
  int keptChans = (int)channels_.size();
  std::snprintf(buf, sizeof(buf),
    "skinned=%s, bones=%d->%d, channels=%d->%d, weightedVerts=%zu, anim=(duration=%.3f ticks, tps=%.3f)",
    skinned_?"true":"false",
    (int)originalBoneCount_, keptBones,
    (int)originalChannelCount_, keptChans,
    weightedVertexCount_,
    animDuration_, animTicksPerSecond_);
  return std::string(buf);
}

void ModelRenderer::ensureBuffers(const MeshData& mesh){
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size()*sizeof(float), mesh.vertices.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size()*sizeof(unsigned int), mesh.indices.data(), GL_STATIC_DRAW);
  // layout: pos(3), normal(3), uv(2), boneIdx(4f), boneW(4)
  GLsizei stride = 16*sizeof(float);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,(void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,stride,(void*)(3*sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,stride,(void*)(6*sizeof(float)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,stride,(void*)(8*sizeof(float)));
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4,4,GL_FLOAT,GL_FALSE,stride,(void*)(12*sizeof(float)));
  glBindVertexArray(0);
}

bool ModelRenderer::loadModel(const std::string& path, std::string& log){
  MeshData mesh;
  if(!OBJLoader::load(path, mesh, log)) return false;
  return setMesh(mesh);
}

bool ModelRenderer::setMesh(const MeshData& mesh){
  // 准备可写副本（用于可能的骨骼压缩与重映射）
  std::vector<float> vtx = mesh.vertices;
  std::vector<unsigned int> idx = mesh.indices;
  std::vector<MeshData::Bone> bones = mesh.bones;
  std::vector<MeshData::Channel> channels = mesh.channels;
  // 记录原始骨骼/通道数量
  originalBoneCount_ = bones.size();
  originalChannelCount_ = channels.size();

  // 回退到早期行为：不对无骨骼但有权重的情况做强制清零，保持原始数据（便于复现当时的状态）

  // 若骨骼过多，压缩到 MAX_BONES：选择高影响力骨骼并闭包其祖先，顶点权重重映射到最近保留祖先
  if(mesh.skinned && bones.size() > (size_t)MAX_BONES){
    size_t nb = bones.size();
    std::vector<double> boneWeightSum(nb, 0.0);
    const size_t strideF = 16;
    for(size_t i=0;i+strideF<=vtx.size(); i+=strideF){
      for(int k=0;k<4;++k){
        int bi = (int)std::round(vtx[i+8+k]);
        float w = vtx[i+12+k];
        if(bi>=0 && (size_t)bi<nb && w>0.0f){ boneWeightSum[bi] += (double)w; }
      }
    }
    // 标记哪些骨骼有动画通道（优先保留有通道的骨骼）
    std::vector<char> hasChan(nb, 0);
    for(const auto& ch : channels){ if(ch.boneIndex>=0 && (size_t)ch.boneIndex<nb) hasChan[ch.boneIndex] = 1; }
    // 排序骨骼索引：优先 hasChan=true，其次按累计权重降序
    std::vector<int> order(nb); for(size_t i=0;i<nb;++i) order[i]=(int)i;
    std::sort(order.begin(), order.end(), [&](int a,int b){
      if(hasChan[a] != hasChan[b]) return hasChan[a] > hasChan[b];
      return boneWeightSum[a] > boneWeightSum[b];
    });
    // 选择集合：保证祖先闭包
    std::vector<char> keep(nb, 0);
    int kept = 0;
    for(int bi : order){
      if(kept >= MAX_BONES) break;
      if(boneWeightSum[bi] <= 0.0 && kept>0) continue; // 权重为 0 的尾部骨骼可跳过
      // 收集到根的路径
      std::vector<int> path;
      int cur = bi;
      while(cur>=0 && !keep[cur]){ path.push_back(cur); cur = bones[cur].parent; }
      if(kept + (int)path.size() <= MAX_BONES){
        for(int p : path){ keep[p]=1; ++kept; }
      }
    }
    // 至少保留一个根（若未选中）
    if(kept==0){
      for(size_t i=0;i<nb;++i){ if(bones[i].parent<0){ keep[i]=1; kept=1; break; } }
    }
    // old->new 索引映射
    std::vector<int> mapOldToNew(nb, -1);
    std::vector<int> keptList; keptList.reserve(kept);
    for(size_t i=0;i<nb;++i){ if(keep[i]){ mapOldToNew[i] = (int)keptList.size(); keptList.push_back((int)i); } }
    // 重新映射父索引并构建新骨骼数组
    std::vector<MeshData::Bone> newBones; newBones.resize(keptList.size());
    for(size_t ni=0; ni<keptList.size(); ++ni){
      int old = keptList[ni];
      newBones[ni] = bones[old];
      int p = bones[old].parent;
      while(p>=0 && mapOldToNew[p]<0) p = bones[p].parent; // 指向最近的保留祖先
      newBones[ni].parent = (p>=0)? mapOldToNew[p] : -1;
    }
    // 顶点重映射：将脱落骨骼的影响合并到最近保留祖先
    for(size_t i=0;i+strideF<=vtx.size(); i+=strideF){
      int outIdx[4] = {-1,-1,-1,-1};
      float outW[4] = {0,0,0,0};
      int outCount = 0;
      for(int k=0;k<4;++k){
        int biOld = (int)std::round(vtx[i+8+k]);
        float w = vtx[i+12+k];
        if(w<=0.0f || biOld<0 || (size_t)biOld>=nb) continue;
        int bi = biOld;
        // 向上寻找最近保留的祖先
        while(bi>=0 && !keep[bi]) bi = bones[bi].parent;
        if(bi<0) continue; // 找不到合适祖先则忽略该权重
        int newBi = mapOldToNew[bi];
        // 合并到输出 4 槽中
        int slot=-1; for(int s=0;s<outCount;++s){ if(outIdx[s]==newBi){ slot=s; break; } }
        if(slot<0){
          if(outCount<4){ slot=outCount++; outIdx[slot]=newBi; outW[slot]=0.0f; }
          else{
            // 挤掉权重最小的槽
            int minS=0; for(int s=1;s<4;++s){ if(outW[s]<outW[minS]) minS=s; }
            if(outW[minS] < w){ outIdx[minS]=newBi; outW[minS]=0.0f; slot=minS; }
            else slot=-1;
          }
        }
        if(slot>=0) outW[slot] += w;
      }
      // 归一化并写回
      float sum = outW[0]+outW[1]+outW[2]+outW[3]; if(sum>1e-6f){ for(int s=0;s<4;++s) outW[s]/=sum; }
      for(int s=0;s<4;++s){ vtx[i+8+s] = (outIdx[s]>=0)? (float)outIdx[s] : 0.0f; vtx[i+12+s] = outW[s]; }
    }
    // 渠道重映射：仅保留保留集合中的骨骼通道
    std::vector<MeshData::Channel> newChans; newChans.reserve(channels.size());
    for(const auto& ch : channels){ if(ch.boneIndex>=0 && (size_t)ch.boneIndex<nb && keep[ch.boneIndex]){ MeshData::Channel c = ch; c.boneIndex = mapOldToNew[ch.boneIndex]; newChans.push_back(std::move(c)); } }
    bones.swap(newBones);
    channels.swap(newChans);
  }

  // 上传缓冲
  indexCount_ = idx.size();
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, vtx.size()*sizeof(float), vtx.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
  // layout: pos(3), normal(3), uv(2), boneIdx(4f), boneW(4)
  {
    GLsizei stride = 16*sizeof(float);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,stride,(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,stride,(void*)(6*sizeof(float)));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,stride,(void*)(8*sizeof(float)));
    glEnableVertexAttribArray(4); glVertexAttribPointer(4,4,GL_FLOAT,GL_FALSE,stride,(void*)(12*sizeof(float)));
    glBindVertexArray(0);
  }

  // 保存蒙皮与动画数据（可能已压缩重映射）
  skinned_ = mesh.skinned;
  bones_ = std::move(bones);
  channels_ = std::move(channels);
  animDuration_ = mesh.animDuration;
  animTicksPerSecond_ = mesh.animTicksPerSecond;
  // 统计有权重的顶点数
  weightedVertexCount_ = 0;
  if(!vtx.empty()){
    const size_t strideF = 16;
    for(size_t i=0;i+strideF<=vtx.size(); i+=strideF){
      float s = vtx[i+12]+vtx[i+13]+vtx[i+14]+vtx[i+15];
      if(s > 1e-6f) ++weightedVertexCount_;
    }
  }
  bonePalette_.assign(MAX_BONES*16, 0.0f);
  for(int i=0;i<MAX_BONES;++i){ float* m = &bonePalette_[i*16]; for(int j=0;j<16;++j) m[j] = (j%5==0)?1.0f:0.0f; }
  // 计算 AABB 中心，作为旋转/缩放的枢轴
  if(!vtx.empty()){
    float minv[3] = { vtx[0], vtx[1], vtx[2] };
    float maxv[3] = { vtx[0], vtx[1], vtx[2] };
    const size_t strideF = 16;
    for(size_t i=0;i<vtx.size(); i+=strideF){
      float x = vtx[i+0];
      float y = vtx[i+1];
      float z = vtx[i+2];
      if(x<minv[0]) minv[0]=x; if(y<minv[1]) minv[1]=y; if(z<minv[2]) minv[2]=z;
      if(x>maxv[0]) maxv[0]=x; if(y>maxv[1]) maxv[1]=y; if(z>maxv[2]) maxv[2]=z;
    }
    center_[0] = 0.5f*(minv[0]+maxv[0]);
    center_[1] = 0.5f*(minv[1]+maxv[1]);
    center_[2] = 0.5f*(minv[2]+maxv[2]);
    float dx = maxv[0]-minv[0];
    float dy = maxv[1]-minv[1];
    float dz = maxv[2]-minv[2];
    float maxAxis = std::max(dx, std::max(dy, dz));
    normalizeFactor_ = (maxAxis > 1e-6f) ? (1.0f / maxAxis) : 1.0f;
  } else {
    center_[0]=center_[1]=center_[2]=0.f;
    normalizeFactor_ = 1.0f;
  }
  return true;
}

void ModelRenderer::setParams(float scale, const float rotDeg[3], const float baseColor[3], const float lightDir[3]){
  scale_ = scale; rot_[0]=rotDeg[0]; rot_[1]=rotDeg[1]; rot_[2]=rotDeg[2];
  baseColor_[0]=baseColor[0]; baseColor_[1]=baseColor[1]; baseColor_[2]=baseColor[2];
}

// pivot 参数已移除，改为 Position + Auto Center + Normalize 控制

static void makePerspective(float* m, float fovyDeg, float aspect, float znear, float zfar){
  float f = 1.0f/std::tan(fovyDeg*0.5f*M_PI/180.0f);
  m[0]=f/aspect; m[1]=0; m[2]=0; m[3]=0;
  m[4]=0; m[5]=f; m[6]=0; m[7]=0;
  m[8]=0; m[9]=0; m[10]=(zfar+znear)/(znear-zfar); m[11]=-1;
  m[12]=0; m[13]=0; m[14]=(2*zfar*znear)/(znear-zfar); m[15]=0;
}

static void makeIdentity(float* m){ for(int i=0;i<16;++i) m[i]=(i%5==0)?1.0f:0.0f; }
// Column-major 4x4 multiply: r = a * b
static void multiply(float* r, const float* a, const float* b){
  float t[16];
  for(int j=0;j<4;++j){
    for(int i=0;i<4;++i){
      t[j*4+i] = a[0*4+i]*b[j*4+0] + a[1*4+i]*b[j*4+1] + a[2*4+i]*b[j*4+2] + a[3*4+i]*b[j*4+3];
    }
  }
  for(int i=0;i<16;++i) r[i]=t[i];
}
// Column-major rotation matrices (right-handed, radians)
static void makeRotation(float* m, float rx, float ry, float rz){
  float cx=cosf(rx), sx=sinf(rx), cy=cosf(ry), sy=sinf(ry), cz=cosf(rz), sz=sinf(rz);
  float Rx[16]; makeIdentity(Rx);
  Rx[5]=cx;  Rx[6]=sx;  Rx[9]=-sx; Rx[10]=cx;
  float Ry[16]; makeIdentity(Ry);
  Ry[0]=cy;  Ry[8]=sy;  Ry[2]=-sy; Ry[10]=cy;
  float Rz[16]; makeIdentity(Rz);
  Rz[0]=cz;  Rz[4]=-sz; Rz[1]=sz;  Rz[5]=cz;
  float Rxy[16]; multiply(Rxy, Ry, Rx); // Y then X
  multiply(m, Rz, Rxy);                 // then Z
}
static void makeScale(float* m, float s){ makeIdentity(m); m[0]=m[5]=m[10]=s; }
static void makeTranslate(float* m, float x,float y,float z){ makeIdentity(m); m[12]=x; m[13]=y; m[14]=z; }

static void makeLookAt(float* m, const float eye[3], const float center[3], const float up[3]){
  // f = normalize(center - eye)
  float fx = center[0]-eye[0], fy=center[1]-eye[1], fz=center[2]-eye[2];
  float fl = std::sqrt(fx*fx+fy*fy+fz*fz); if(fl<1e-6f){ fx=0; fy=0; fz=-1; fl=1; }
  fx/=fl; fy/=fl; fz/=fl;
  // s = normalize(f x up)
  float sx = fy*up[2]-fz*up[1];
  float sy = fz*up[0]-fx*up[2];
  float sz = fx*up[1]-fy*up[0];
  float sl = std::sqrt(sx*sx+sy*sy+sz*sz); if(sl<1e-6f){ sx=1; sy=0; sz=0; sl=1; }
  sx/=sl; sy/=sl; sz/=sl;
  // u = s x f
  float ux = sy*fz - sz*fy;
  float uy = sz*fx - sx*fz;
  float uz = sx*fy - sy*fx;
  float R[16]; makeIdentity(R);
  R[0]=sx; R[4]=sy; R[8]=sz;
  R[1]=ux; R[5]=uy; R[9]=uz;
  R[2]=-fx; R[6]=-fy; R[10]=-fz;
  float T[16]; makeTranslate(T, -eye[0], -eye[1], -eye[2]);
  multiply(m, R, T);
}

  // 评估并上传骨骼矩阵
void ModelRenderer::uploadBones(float animTimeSec){
  GLint locCount = shader_.uniformLoc("uBoneCount");
  GLint locBones = shader_.uniformLoc("uBones[0]");
  if(locCount < 0 || locBones < 0){
    return; // shader 未声明骨骼路径，直接返回
  }
  int boneCount = (int)std::min((size_t)MAX_BONES, bones_.size());
  if(!skinned_ || boneCount <= 0){
    // 无蒙皮：上传 1 个单位矩阵
    glUniform1i(locCount, 1);
    float id[16]; makeIdentity(id);
    glUniformMatrix4fv(locBones, 1, GL_FALSE, id);
    return;
  }

  // 将时间映射为 ticks（循环）
  double ticks = animTimeSec * (animTicksPerSecond_ > 0.0 ? animTicksPerSecond_ : 25.0);
  if(animDuration_ > 0.0){
    ticks = fmod(ticks, animDuration_);
  }

  // 建立 boneIndex -> channel 指针的快速查找（允许无动画：此时使用 bindLocal 作为默认局部）
  std::vector<const MeshData::Channel*> chanOf(bones_.size(), nullptr);
  for(const auto& ch : channels_){ if(ch.boneIndex>=0 && (size_t)ch.boneIndex<chanOf.size()) chanOf[ch.boneIndex] = &ch; }

  auto lerp = [](float a,float b,float t){ return a + (b-a)*t; };
  auto slerp = [=](const float q0[4], const float q1in[4], float t, float out[4]){
    // 规范化并处理最近路径
    float q1[4] = {q1in[0],q1in[1],q1in[2],q1in[3]};
    float dot = q0[0]*q1[0]+q0[1]*q1[1]+q0[2]*q1[2]+q0[3]*q1[3];
    if(dot < 0.0f){ dot = -dot; q1[0]=-q1[0]; q1[1]=-q1[1]; q1[2]=-q1[2]; q1[3]=-q1[3]; }
    const float EPS=1e-5f;
    if(1.0f - dot < EPS){ out[0]=lerp(q0[0],q1[0],t); out[1]=lerp(q0[1],q1[1],t); out[2]=lerp(q0[2],q1[2],t); out[3]=lerp(q0[3],q1[3],t); return; }
    float theta = acosf(dot);
    float s = sinf(theta);
    float w0 = sinf((1.0f-t)*theta)/s;
    float w1 = sinf(t*theta)/s;
    out[0] = w0*q0[0] + w1*q1[0];
    out[1] = w0*q0[1] + w1*q1[1];
    out[2] = w0*q0[2] + w1*q1[2];
    out[3] = w0*q0[3] + w1*q1[3];
  };
  auto quatToMat = [](const float q[4], float* m){
    float x=q[0], y=q[1], z=q[2], w=q[3];
    float xx=x*x, yy=y*y, zz=z*z; float xy=x*y, xz=x*z, yz=y*z; float wx=w*x, wy=w*y, wz=w*z;
    // 列主序 3x3 填入 4x4
    m[0]=1-2*(yy+zz); m[4]=2*(xy - wz); m[8] =2*(xz + wy); m[12]=0;
    m[1]=2*(xy + wz); m[5]=1-2*(xx+zz); m[9] =2*(yz - wx); m[13]=0;
    m[2]=2*(xz - wy); m[6]=2*(yz + wx); m[10]=1-2*(xx+yy); m[14]=0;
    m[3]=0; m[7]=0; m[11]=0; m[15]=1;
  };
  auto makeScaleM = [](float* m, float sx,float sy,float sz){ makeIdentity(m); m[0]=sx; m[5]=sy; m[10]=sz; };
  // 评估每个骨骼的局部 TRS（若无通道，则使用 bindLocal）
  const size_t nb = bones_.size();
  std::vector<float> local(nb*16, 0.0f);
  for(size_t i=0;i<nb;++i){
    float T[16]; float R[16]; float S[16];
    makeTranslate(T, 0,0,0);
    float qid[4] = {0,0,0,1}; quatToMat(qid, R);
    makeScaleM(S,1,1,1);
    const MeshData::Channel* ch = (i<chanOf.size())? chanOf[i] : nullptr;
    if(ch){
      // 先用 bindLocal 作为默认 TRS，再用存在的动画通道覆盖对应分量
      const float* M0 = bones_[i].bindLocal; // 列主序
      // 默认：从 bindLocal 拆出 TRS
      float tX0 = M0[12], tY0 = M0[13], tZ0 = M0[14];
      makeTranslate(T, tX0, tY0, tZ0);
      float c0_[3] = { M0[0], M0[1], M0[2] };
      float c1_[3] = { M0[4], M0[5], M0[6] };
      float c2_[3] = { M0[8], M0[9], M0[10] };
      float sx0 = std::sqrt(c0_[0]*c0_[0]+c0_[1]*c0_[1]+c0_[2]*c0_[2]); if(sx0<1e-6f) sx0=1.0f;
      float sy0 = std::sqrt(c1_[0]*c1_[0]+c1_[1]*c1_[1]+c1_[2]*c1_[2]); if(sy0<1e-6f) sy0=1.0f;
      float sz0 = std::sqrt(c2_[0]*c2_[0]+c2_[1]*c2_[1]+c2_[2]*c2_[2]); if(sz0<1e-6f) sz0=1.0f;
      makeScaleM(S, sx0, sy0, sz0);
      c0_[0]/=sx0; c0_[1]/=sx0; c0_[2]/=sx0;
      c1_[0]/=sy0; c1_[1]/=sy0; c1_[2]/=sy0;
      c2_[0]/=sz0; c2_[1]/=sz0; c2_[2]/=sz0;
      R[0]=c0_[0]; R[1]=c0_[1]; R[2]=c0_[2]; R[3]=0;
      R[4]=c1_[0]; R[5]=c1_[1]; R[6]=c1_[2]; R[7]=0;
      R[8]=c2_[0]; R[9]=c2_[1]; R[10]=c2_[2]; R[11]=0;
      R[12]=0; R[13]=0; R[14]=0; R[15]=1;

      // 覆盖：位置
      if(!ch->posKeys.empty()){
        if(ch->posKeys.size()==1){ makeTranslate(T, ch->posKeys[0].v[0], ch->posKeys[0].v[1], ch->posKeys[0].v[2]); }
        else{
          size_t k1=0; while(k1+1<ch->posKeys.size() && ch->posKeys[k1+1].t <= ticks) ++k1;
          size_t k2 = std::min(k1+1, ch->posKeys.size()-1);
          double t0=ch->posKeys[k1].t, t1=ch->posKeys[k2].t; float a = (t1>t0)? (float)((ticks - t0)/(t1 - t0)) : 0.0f;
          float x = lerp(ch->posKeys[k1].v[0], ch->posKeys[k2].v[0], a);
          float y = lerp(ch->posKeys[k1].v[1], ch->posKeys[k2].v[1], a);
          float z = lerp(ch->posKeys[k1].v[2], ch->posKeys[k2].v[2], a);
          makeTranslate(T, x,y,z);
        }
      }
      // 覆盖：旋转
      if(!ch->rotKeys.empty()){
        float q[4] = {0,0,0,1};
        if(ch->rotKeys.size()==1){ q[0]=ch->rotKeys[0].q[0]; q[1]=ch->rotKeys[0].q[1]; q[2]=ch->rotKeys[0].q[2]; q[3]=ch->rotKeys[0].q[3]; }
        else{
          size_t k1=0; while(k1+1<ch->rotKeys.size() && ch->rotKeys[k1+1].t <= ticks) ++k1;
          size_t k2 = std::min(k1+1, ch->rotKeys.size()-1);
          double t0=ch->rotKeys[k1].t, t1=ch->rotKeys[k2].t; float a = (t1>t0)? (float)((ticks - t0)/(t1 - t0)) : 0.0f;
          slerp(ch->rotKeys[k1].q, ch->rotKeys[k2].q, a, q);
        }
        quatToMat(q, R);
      }
      // 覆盖：缩放
      if(!ch->sclKeys.empty()){
        if(ch->sclKeys.size()==1){ makeScaleM(S, ch->sclKeys[0].v[0], ch->sclKeys[0].v[1], ch->sclKeys[0].v[2]); }
        else{
          size_t k1=0; while(k1+1<ch->sclKeys.size() && ch->sclKeys[k1+1].t <= ticks) ++k1;
          size_t k2 = std::min(k1+1, ch->sclKeys.size()-1);
          double t0=ch->sclKeys[k1].t, t1=ch->sclKeys[k2].t; float a = (t1>t0)? (float)((ticks - t0)/(t1 - t0)) : 0.0f;
          float sx = lerp(ch->sclKeys[k1].v[0], ch->sclKeys[k2].v[0], a);
          float sy = lerp(ch->sclKeys[k1].v[1], ch->sclKeys[k2].v[1], a);
          float sz = lerp(ch->sclKeys[k1].v[2], ch->sclKeys[k2].v[2], a);
          makeScaleM(S, sx,sy,sz);
        }
      }
    } else {
      // 无动画通道：从 bindLocal 拆出 TRS
      const float* M = bones_[i].bindLocal; // 列主序
      // 近似分解：T 取最后一列；R 从 3x3，S 从列向量长度
      float tX = M[12], tY = M[13], tZ = M[14];
      makeTranslate(T, tX, tY, tZ);
      // 提取列向量
      float c0[3] = { M[0], M[1], M[2] };
      float c1[3] = { M[4], M[5], M[6] };
      float c2[3] = { M[8], M[9], M[10] };
      float sx = std::sqrt(c0[0]*c0[0]+c0[1]*c0[1]+c0[2]*c0[2]); if(sx<1e-6f) sx=1.0f;
      float sy = std::sqrt(c1[0]*c1[0]+c1[1]*c1[1]+c1[2]*c1[2]); if(sy<1e-6f) sy=1.0f;
      float sz = std::sqrt(c2[0]*c2[0]+c2[1]*c2[1]+c2[2]*c2[2]); if(sz<1e-6f) sz=1.0f;
      makeScaleM(S, sx, sy, sz);
      // 归一化得到旋转矩阵列
      c0[0]/=sx; c0[1]/=sx; c0[2]/=sx;
      c1[0]/=sy; c1[1]/=sy; c1[2]/=sy;
      c2[0]/=sz; c2[1]/=sz; c2[2]/=sz;
      R[0]=c0[0]; R[1]=c0[1]; R[2]=c0[2]; R[3]=0;
      R[4]=c1[0]; R[5]=c1[1]; R[6]=c1[2]; R[7]=0;
      R[8]=c2[0]; R[9]=c2[1]; R[10]=c2[2]; R[11]=0;
      R[12]=0; R[13]=0; R[14]=0; R[15]=1;
    }
    float TR[16]; multiply(TR, T, R);
    float TRS[16]; multiply(TRS, TR, S);
    float* dst = &local[i*16]; for(int j=0;j<16;++j) dst[j]=TRS[j];
  }

  // 递归累乘父变换，获得全局矩阵
  std::vector<float> global(nb*16, 0.0f);
  for(size_t i=0;i<nb;++i){
    if(bones_[i].parent < 0){
      float* g = &global[i*16];
      float* l = &local[i*16];
      for(int j=0;j<16;++j) g[j]=l[j];
    }else{
      // g = g_parent * l
      float* g = &global[i*16];
      const float* gp = &global[bones_[i].parent*16];
      const float* l = &local[i*16];
      multiply(g, gp, l);
    }
  }

  // 最终骨骼矩阵：使用 global*offset
  for(int i=0;i<boneCount; ++i){
    const float* g = &global[i*16];
    const float* off = bones_[i].offset;
    float* outM = &bonePalette_[i*16];
    multiply(outM, g, off);
  }
  glUniform1i(locCount, boneCount);
  glUniformMatrix4fv(locBones, boneCount, GL_FALSE, bonePalette_.data());
}

void ModelRenderer::render(int vpWidth, int vpHeight){
  if(indexCount_==0 || shader_.program()==0) return;
  shader_.use();

  // 投影
  float Proj[16]; makePerspective(Proj, 45.0f, (float)vpWidth/(float)vpHeight, 0.1f, 100.0f);
  // 相机绕轨道（以场景原点为目标点）。
  // 说明：当 Auto Center 开启（默认），模型中心被移到原点，再平移到 position_。
  // 为了让 Position 真正影响屏幕上的位置，这里将相机的注视点固定在原点，
  // 而不是跟随 position_，否则模型与相机一起移动，看起来“Position 不起作用”。
  float target[3] = { 0.f, 0.f, 0.f };
  float yaw = camYawDeg_*M_PI/180.0f;
  float pitch = camPitchDeg_*M_PI/180.0f;
  float cp = cosf(pitch), sp = sinf(pitch);
  float cy = cosf(yaw),   sy = sinf(yaw);
  float eye[3] = {
    target[0] + camRadius_ * cp * sy,
    target[1] + camRadius_ * sp,
    target[2] + camRadius_ * cp * cy
  };
  float up[3] = {0.f, 1.f, 0.f};
  float V[16]; makeLookAt(V, eye, target, up);
  float R[16]; makeRotation(R, rot_[0]*M_PI/180.0f, rot_[1]*M_PI/180.0f, rot_[2]*M_PI/180.0f);
  float effectiveScale = scale_ * (normalize_ ? normalizeFactor_ : 1.0f);
  float S[16]; makeScale(S, effectiveScale);
  float RS[16]; multiply(RS,R,S);
  // 自适应居中：若开启，将 AABB 中心移到原点再应用缩放/旋转；之后整体平移到 position_
  float M[16];
  if(autoCenter_){
    // M = T(position) * R * S * T(-center)
    float Tneg[16]; makeTranslate(Tneg, -center_[0], -center_[1], -center_[2]);
    float RS_Tneg[16]; multiply(RS_Tneg, RS, Tneg);
    float Tpos[16]; makeTranslate(Tpos, position_[0], position_[1], position_[2]);
    multiply(M, Tpos, RS_Tneg);
  }else{
    // 不居中时：M = T(position) * R * S
    float Tpos[16]; makeTranslate(Tpos, position_[0], position_[1], position_[2]);
    multiply(M, Tpos, RS);
  }

  GLint loc;
  loc = shader_.uniformLoc("uProj"); if(loc>=0) glUniformMatrix4fv(loc,1,GL_FALSE,Proj);
  loc = shader_.uniformLoc("uView"); if(loc>=0) glUniformMatrix4fv(loc,1,GL_FALSE,V);
  loc = shader_.uniformLoc("uModel"); if(loc>=0) glUniformMatrix4fv(loc,1,GL_FALSE,M);
  loc = shader_.uniformLoc("uBaseColor"); if(loc>=0) glUniform3fv(loc,1,baseColor_);
  // 摄像机位置（世界空间）
  loc = shader_.uniformLoc("uEyePos"); if(loc>=0) glUniform3fv(loc,1,eye);
  // 三盏方向光由轨道角计算
  float ldirs[9];
  float lcols[9];
  float lshine[3];
  for(int i=0;i<3;++i){
    float ly = lightYawDeg_[i]*M_PI/180.0f;
    float lp = lightPitchDeg_[i]*M_PI/180.0f;
    float lcp = cosf(lp), lsp = sinf(lp);
    float lcy = cosf(ly), lsy = sinf(ly);
    float dir[3] = { lcp*lsy, lsp, lcp*lcy };
    // 归一化
    float ll = std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]); if(ll>1e-6f){ dir[0]/=ll; dir[1]/=ll; dir[2]/=ll; }
    ldirs[i*3+0]=dir[0]; ldirs[i*3+1]=dir[1]; ldirs[i*3+2]=dir[2];
    lcols[i*3+0]=lightColor_[i][0]; lcols[i*3+1]=lightColor_[i][1]; lcols[i*3+2]=lightColor_[i][2];
    lshine[i]=lightShine_[i];
  }
  // 数组 uniform：查询第一个元素的 location，并一次性传 3 个
  loc = shader_.uniformLoc("uLightDir[0]"); if(loc>=0) glUniform3fv(loc,3,ldirs);
  loc = shader_.uniformLoc("uLightColor[0]"); if(loc>=0) glUniform3fv(loc,3,lcols);
  loc = shader_.uniformLoc("uLightShine[0]"); if(loc>=0) glUniform1fv(loc,3,lshine);
  // 效果 uniforms
  // 不再上传 uMode，效果统一并行生效
  loc = shader_.uniformLoc("uDispAmt"); if(loc>=0) glUniform1f(loc, dispAmt_);
  loc = shader_.uniformLoc("uDispFreq"); if(loc>=0) glUniform1f(loc, dispFreq_);
  loc = shader_.uniformLoc("uExplodeAmt"); if(loc>=0) glUniform1f(loc, explodeAmt_);
  loc = shader_.uniformLoc("uCellSize"); if(loc>=0) glUniform1f(loc, cellSize_);
  loc = shader_.uniformLoc("uSeed"); if(loc>=0) glUniform1f(loc, seed_);
  loc = shader_.uniformLoc("uExplodeT"); if(loc>=0) glUniform1f(loc, explodeT_);
  loc = shader_.uniformLoc("uGravity"); if(loc>=0) glUniform1f(loc, gravity_);
  loc = shader_.uniformLoc("uSpin"); if(loc>=0) glUniform1f(loc, spin_);
  // 连续时间（秒）用于持续自转（恢复）
  {
    using namespace std::chrono;
    // 效果时间（用于噪声等）始终使用真实经过时间
    float effectTimeSec = duration_cast<duration<float>>(steady_clock::now() - startTime_).count();
    loc = shader_.uniformLoc("uTimeSec"); if(loc>=0) glUniform1f(loc, effectTimeSec);
    // 动画时间：播放模式按速度推进；否则使用手动时间（0..1）
    double tps = (animTicksPerSecond_ > 0.0) ? animTicksPerSecond_ : 25.0;
    double durationSec = (tps > 0.0) ? (animDuration_ / tps) : 0.0;
    float animTimeSec = 0.0f;
    if(animEnabled_){
      if(durationSec > 0.0){
        animTimeSec = fmodf(effectTimeSec * std::max(0.0f, animSpeed_), (float)durationSec);
      }else{
        animTimeSec = effectTimeSec * std::max(0.0f, animSpeed_);
      }
    }else{
      animTimeSec = (float)(durationSec * std::max(0.0f, std::min(1.0f, animTime01_)));
    }
    // 上传骨骼矩阵（若有）
    uploadBones(animTimeSec);
  }
  loc = shader_.uniformLoc("uTwistAmt"); if(loc>=0) glUniform1f(loc, twistAmt_);
  loc = shader_.uniformLoc("uStretchX"); if(loc>=0) glUniform1f(loc, stretch_[0]);
  loc = shader_.uniformLoc("uStretchY"); if(loc>=0) glUniform1f(loc, stretch_[1]);
  loc = shader_.uniformLoc("uStretchZ"); if(loc>=0) glUniform1f(loc, stretch_[2]);
  loc = shader_.uniformLoc("uVoxelSize"); if(loc>=0) glUniform1f(loc, voxelSize_);
  loc = shader_.uniformLoc("uWireThick"); if(loc>=0) glUniform1f(loc, wireThick_);
  loc = shader_.uniformLoc("uWireColor"); if(loc>=0) glUniform3fv(loc,1,wireColor_);
  loc = shader_.uniformLoc("uDecimate"); if(loc>=0) glUniform1f(loc, decimate_);
  loc = shader_.uniformLoc("uGridSize"); if(loc>=0) glUniform1f(loc, gridSize_);
  loc = shader_.uniformLoc("uSketchOn"); if(loc>=0) glUniform1f(loc, sketchOn_ ? 1.0f : 0.0f);
  // effect 相关已移除，不再设置对应 uniform（shader 中默认为 0）

  glBindVertexArray(vao_);
  GLsizei drawCount = (indexCount_ > (size_t)std::numeric_limits<GLsizei>::max())
                        ? std::numeric_limits<GLsizei>::max()
                        : (GLsizei)indexCount_;
  glDrawElements(GL_TRIANGLES, drawCount, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);
}

void ModelRenderer::release(){
  if(ebo_) glDeleteBuffers(1,&ebo_); ebo_=0;
  if(vbo_) glDeleteBuffers(1,&vbo_); vbo_=0;
  if(vao_) glDeleteVertexArrays(1,&vao_); vao_=0;
}

// ================== FFGL 粘合层（基于 Resolume FFGL SDK） ==================

// 参数索引
enum ParamIndex : FFUInt32 {
  PI_ModelPath,
  PI_Status,
  PI_ForceBake,
  PI_ImportMode,
  // Animation
  PI_AnimOn,
  PI_AnimSpeed,
  PI_AnimTime,
  PI_Scale,
  PI_RotX, PI_RotY, PI_RotZ,
  PI_BaseH, PI_BaseS, PI_BaseL,
  PI_PosX, PI_PosY, PI_PosZ,
  PI_AutoCenter,
  PI_NormalizeScale,
  PI_CamRadius, PI_CamYaw, PI_CamPitch,
  // Light 1
  PI_L1Yaw, PI_L1Pitch, PI_L1H, PI_L1S, PI_L1L, PI_L1Size,
  // Light 2
  PI_L2Yaw, PI_L2Pitch, PI_L2H, PI_L2S, PI_L2L, PI_L2Size,
  // Light 3
  PI_L3Yaw, PI_L3Pitch, PI_L3H, PI_L3S, PI_L3L, PI_L3Size,
  // Effect params
  PI_DispAmt, PI_DispFreq,
  PI_ExplodeAmt, PI_CellSize, PI_Seed,
  PI_ExplodeTime, PI_Gravity, PI_Spin,
  PI_TwistAmt, PI_StretchX, PI_StretchY, PI_StretchZ,
  PI_VoxelSize,
  PI_SketchOn,
  PI_WireThick, PI_WireH, PI_WireS, PI_WireL, PI_GridSize,
  PI_Decimate,
};

static std::string GetBundleShadersDir(){
  Dl_info info{};
  if(dladdr((void*)&GetBundleShadersDir, &info) && info.dli_fname){
    std::filesystem::path p(info.dli_fname);
    // <bundle>/Contents/MacOS/<binary> -> <bundle>/Contents/Resources/shaders
    auto shaders = p.parent_path().parent_path() / "Resources" / "shaders";
    return shaders.string();
  }
  return std::string("shaders");
}

static std::string GetBundleDefaultModel(){
  Dl_info info{};
  if(dladdr((void*)&GetBundleDefaultModel, &info) && info.dli_fname){
    std::filesystem::path p(info.dli_fname);
    auto model = p.parent_path().parent_path() / "Resources" / "model.obj";
    return model.string();
  }
  return std::string();
}

class FFGLModelFXPlugin : public CFFGLPlugin {
public:
  FFGLModelFXPlugin()
  : viewportW(1280), viewportH(720)
  {
    // 输入路数
    SetMinInputs(0);
    SetMaxInputs(0);

    // 参数元信息
    // 文件选择参数：支持 .obj/.fbx/.gltf/.glb
    {
  std::vector<std::string> exts = {"obj","fbx","gltf","glb"};
      std::string def = GetBundleDefaultModel();
      if(def.empty() || !std::filesystem::exists(def))
        def = DefaultModelPath();
      SetFileParamInfo(PI_ModelPath, "Model", std::move(exts), def.c_str());
    }
    // 状态文本（只读）：用于显示最近一次加载状态/错误
    SetParamInfo(PI_Status, "Status", FF_TYPE_TEXT, "Ready");
    // FBX 强制静态烘焙（跳过动画/蒙皮，最大化静态几何成功率）
    SetParamInfof(PI_ForceBake, "Force Bake (Static)", FF_TYPE_BOOLEAN);
    // 导入模式：0..1 映射为 Auto / SkipFallbacks / StaticBake
    // 0.0-0.33: Auto（默认）| 0.34-0.66: SkipFallbacks（快速失败）| 0.67-1.0: StaticBake（等同于 Force Bake）
    SetParamInfof(PI_ImportMode, "Import Mode", FF_TYPE_STANDARD);
    
  // 动画控制（对带动画/骨骼的模型生效）
  SetParamInfof(PI_AnimOn,   "Anim On",   FF_TYPE_BOOLEAN);
  SetParamInfof(PI_AnimSpeed,"Anim Speed",FF_TYPE_STANDARD);
  SetParamInfof(PI_AnimTime, "Anim Time", FF_TYPE_STANDARD);
    SetParamInfof(PI_Scale, "Scale", FF_TYPE_STANDARD);
    SetParamInfof(PI_RotX, "Rot X", FF_TYPE_STANDARD);
    SetParamInfof(PI_RotY, "Rot Y", FF_TYPE_STANDARD);
    SetParamInfof(PI_RotZ, "Rot Z", FF_TYPE_STANDARD);
  SetParamInfof(PI_BaseH, "Base Hue", FF_TYPE_STANDARD);
  SetParamInfof(PI_BaseS, "Base Sat", FF_TYPE_STANDARD);
  SetParamInfof(PI_BaseL, "Base Light", FF_TYPE_STANDARD);
    SetParamInfof(PI_PosX, "Position X", FF_TYPE_STANDARD);
    SetParamInfof(PI_PosY, "Position Y", FF_TYPE_STANDARD);
    SetParamInfof(PI_PosZ, "Position Z", FF_TYPE_STANDARD);
    SetParamInfof(PI_AutoCenter, "Auto Center", FF_TYPE_BOOLEAN);
    SetParamInfof(PI_NormalizeScale, "Normalize Scale", FF_TYPE_BOOLEAN);
    SetParamInfof(PI_CamRadius, "Cam Radius", FF_TYPE_STANDARD);
  SetParamInfof(PI_CamYaw, "Cam Yaw", FF_TYPE_STANDARD);
  SetParamInfof(PI_CamPitch, "Cam Pitch", FF_TYPE_STANDARD);
  // Light 1
  SetParamInfof(PI_L1Yaw, "Light1 Yaw", FF_TYPE_STANDARD);
  SetParamInfof(PI_L1Pitch, "Light1 Pitch", FF_TYPE_STANDARD);
  SetParamInfof(PI_L1H, "Light1 Hue", FF_TYPE_STANDARD);
  SetParamInfof(PI_L1S, "Light1 Sat", FF_TYPE_STANDARD);
  SetParamInfof(PI_L1L, "Light1 Light", FF_TYPE_STANDARD);
  SetParamInfof(PI_L1Size, "Light1 SpotSize", FF_TYPE_STANDARD);
  // Light 2
  SetParamInfof(PI_L2Yaw, "Light2 Yaw", FF_TYPE_STANDARD);
  SetParamInfof(PI_L2Pitch, "Light2 Pitch", FF_TYPE_STANDARD);
  SetParamInfof(PI_L2H, "Light2 Hue", FF_TYPE_STANDARD);
  SetParamInfof(PI_L2S, "Light2 Sat", FF_TYPE_STANDARD);
  SetParamInfof(PI_L2L, "Light2 Light", FF_TYPE_STANDARD);
  SetParamInfof(PI_L2Size, "Light2 SpotSize", FF_TYPE_STANDARD);
  // Light 3
  SetParamInfof(PI_L3Yaw, "Light3 Yaw", FF_TYPE_STANDARD);
  SetParamInfof(PI_L3Pitch, "Light3 Pitch", FF_TYPE_STANDARD);
  SetParamInfof(PI_L3H, "Light3 Hue", FF_TYPE_STANDARD);
  SetParamInfof(PI_L3S, "Light3 Sat", FF_TYPE_STANDARD);
  SetParamInfof(PI_L3L, "Light3 Light", FF_TYPE_STANDARD);
  SetParamInfof(PI_L3Size, "Light3 SpotSize", FF_TYPE_STANDARD);
  // Effects
  SetParamInfof(PI_DispAmt, "Disp Amount", FF_TYPE_STANDARD);
  SetParamInfof(PI_DispFreq, "Disp Frequency", FF_TYPE_STANDARD);
  SetParamInfof(PI_ExplodeAmt, "Explode Amount", FF_TYPE_STANDARD);
  SetParamInfof(PI_CellSize, "Explode Cell Size", FF_TYPE_STANDARD);
  SetParamInfof(PI_Seed, "Explode Seed", FF_TYPE_STANDARD);
  SetParamInfof(PI_ExplodeTime, "Explode Time", FF_TYPE_STANDARD);
  SetParamInfof(PI_Gravity, "Explode Gravity", FF_TYPE_STANDARD);
  SetParamInfof(PI_Spin, "Explode Spin", FF_TYPE_STANDARD);
  SetParamInfof(PI_TwistAmt, "Twist Amount", FF_TYPE_STANDARD);
  SetParamInfof(PI_StretchX, "Stretch X", FF_TYPE_STANDARD);
  SetParamInfof(PI_StretchY, "Stretch Y", FF_TYPE_STANDARD);
  SetParamInfof(PI_StretchZ, "Stretch Z", FF_TYPE_STANDARD);
  SetParamInfof(PI_VoxelSize, "Voxel Size", FF_TYPE_STANDARD);
  SetParamInfof(PI_SketchOn, "Sketch On", FF_TYPE_BOOLEAN);
  SetParamInfof(PI_WireThick, "Wire Thickness", FF_TYPE_STANDARD);
  SetParamInfof(PI_WireH, "Wire Hue", FF_TYPE_STANDARD);
  SetParamInfof(PI_WireS, "Wire Sat", FF_TYPE_STANDARD);
  SetParamInfof(PI_WireL, "Wire Light", FF_TYPE_STANDARD);
  SetParamInfof(PI_GridSize, "Wire Grid Size", FF_TYPE_STANDARD);
  SetParamInfof(PI_Decimate, "Decimate", FF_TYPE_STANDARD);

    // 默认值
    scale = 1.0f;
    rot[0]=0; rot[1]=0; rot[2]=0;
  // 颜色默认采用 HSL：白色 -> H=0, S=0, L=1
  base[0]=0.0f; base[1]=0.0f; base[2]=1.0f;
    pos[0]=pos[1]=pos[2]=0.5f; // Position 默认 0.5/0.5/0.5
    autoCenter = 1.0f; // 默认开启自适应居中
    normalizeScale = 1.0f; // 默认开启归一化尺寸
    camRadius = 1.0f; camYaw = 0.0f; camPitch = 0.5f; // 默认半径 1、俯仰 0.5
  forceBake = 0.0f; // 默认关闭强制静态烘焙
  importMode = 0.0f; // 0..1 -> Auto
  // 动画默认：开启，速度=1.0（控件 0.5 映射），时间=0
  animOn = 1.0f;
  animSpeed = 0.5f; // 0..1 -> 0..2 倍速，0.5 映射为 1.0x
  animTime = 0.0f;
  // 三盏灯的默认
  lYaw[0]=0.125f; lPitch[0]=0.25f; lColor[0][0]=0.0f; lColor[0][1]=0.0f; lColor[0][2]=1.0f; lSize[0]=0.5f;
  lYaw[1]=0.5f;   lPitch[1]=0.25f; lColor[1][0]=0.0f; lColor[1][1]=0.0f; lColor[1][2]=1.0f; lSize[1]=0.5f;
  lYaw[2]=0.875f; lPitch[2]=0.25f; lColor[2][0]=0.0f; lColor[2][1]=0.0f; lColor[2][2]=1.0f; lSize[2]=0.5f;
  // 效果默认
  // 默认无效果
  dispAmt = 0.0f; dispFreq = 0.5f;       // 位移幅度 0 关闭，频率任意
  explodeAmt = 0.0f; cellSize = 0.2f; seed = 0.0f; // 量=0 关闭
  explodeTime = 0.0f; gravity = 0.0f; spin = 0.0f; // 动力学关闭
  twistAmt = 0.0f;                       // 扭曲 0 关闭
  stretch[0]=0.5f; stretch[1]=0.5f; stretch[2]=0.5f; // 中性值 -> 映射后 1.0
  voxelSize = 0.0f;                      // 0 表示关闭体素化
  sketchOn = 0.0f;                       // 关闭素描
  wireThick = 0.0f; wireCol[0]=0.0f; wireCol[1]=0.0f; wireCol[2]=1.0f; gridSize = 0.2f; // 线框默认白色 HSL
  decimate = 0.0f;                       // 溶解 0 关闭
    loadInProgress = false;
    meshReady = false;
    lastStatus = "Ready";
    shuttingDown = false;
    timedOutNotice = false;
    importStart = std::chrono::steady_clock::time_point{};
    
  }

  // 生命周期
  FFResult InitGL(const FFGLViewportStruct* vp) override {
    if(vp){ viewportW = (int)vp->width; viewportH = (int)vp->height; }
    std::string log;
    const std::string shaderDir = GetBundleShadersDir();
    if(!renderer.init(shaderDir, log)){
      FFGLLog::LogToHost(("Shader init failed: "+log).c_str());
      renderer.release();
      return FF_FAIL;
    }
    std::string modelPath = GetBundleDefaultModel();
    if(modelPath.empty() || !std::filesystem::exists(modelPath))
      modelPath = DefaultModelPath();
    if(std::filesystem::exists(modelPath)){
      if(!renderer.loadModel(modelPath, log)){
        FFGLLog::LogToHost(("Model load failed: "+log).c_str());
        lastStatus = std::string("Init failed: ")+log;
        RaiseParamEvent(PI_Status, FF_EVENT_FLAG_VALUE);
      }else{
        currentModelPath = modelPath;
        lastStatus = std::string("Loaded default: ")+currentModelPath;
        RaiseParamEvent(PI_Status, FF_EVENT_FLAG_VALUE);
      }
    }else{
      FFGLLog::LogToHost("No model file found; using empty scene.");
      lastStatus = "No default model found";
      RaiseParamEvent(PI_Status, FF_EVENT_FLAG_VALUE);
    }

    SyncRendererParams();
    return CFFGLPlugin::InitGL(vp);
  }

  FFResult DeInitGL() override {
    // 退出时不阻塞等待后台导入，避免宿主卡在退出
    shuttingDown = true;
    loadInProgress = false;
    if(loaderThread.joinable()){
      try{ loaderThread.detach(); }catch(...){ }
    }
    renderer.release();
    return FF_SUCCESS;
  }

  FFResult ProcessOpenGL(ProcessOpenGLStruct* /*pGL*/) override {
    // 看门狗提示：导入耗时过长时给出 UI 建议（不强制中断）
    if(loadInProgress && !timedOutNotice){
      using namespace std::chrono;
      auto now = steady_clock::now();
      if(importStart.time_since_epoch().count()!=0){
        auto ms = duration_cast<milliseconds>(now - importStart).count();
        if(ms > (long)importTimeoutMs){
          std::lock_guard<std::mutex> lk(meshMutex);
          lastStatus = "Timeout: import taking too long. Try Import Mode = SkipFallbacks or StaticBake.";
          timedOutNotice = true;
          RaiseParamEvent(PI_Status, FF_EVENT_FLAG_VALUE);
        }
      }
    }
    // 若有新的网格解析完成，上传到 GPU（GL 线程执行）
    if(meshReady){
      std::unique_ptr<MeshData> ready;
      {
        std::lock_guard<std::mutex> lk(meshMutex);
        ready = std::move(pendingMesh);
        meshReady = false;
      }
      if(ready){
        // 只有在有可绘制索引时才视为成功上传，否则保持错误状态
        bool hasTriangles = !ready->indices.empty();
        if(hasTriangles && renderer.setMesh(*ready)){
          currentModelPath = lastRequestedModelPath;
          FFGLLog::LogToHost(("Model uploaded: "+currentModelPath).c_str());
          {
            std::lock_guard<std::mutex> lk(meshMutex);
            // 附加动画/骨骼诊断
            std::string diag = renderer.getAnimStatsString();
            // 附加当前 ForceBake/Anim 开关信息
            char extra[128];
            std::snprintf(extra, sizeof(extra), " | forceBake=%s, animOn=%s, animSpeed=%.2f",
                          (forceBake>0.5f)?"ON":"OFF",
                          (animOn>0.5f)?"ON":"OFF",
                          (2.0f*std::max(0.0f,std::min(1.0f,animSpeed))));
            lastStatus = std::string("Uploaded: ") + currentModelPath + " | " + diag + extra;
          }
          RaiseParamEvent(PI_Status, FF_EVENT_FLAG_VALUE);
        }else{
          // 保持之前的 ERROR 状态信息，如果之前未写入，则给出统一错误
          FFGLLog::LogToHost("GPU upload skipped: no triangles or upload failed");
          {
            std::lock_guard<std::mutex> lk(meshMutex);
            if(lastStatus.empty() || lastStatus.rfind("ERROR:",0)!=0){
              lastStatus = "ERROR: GPU upload failed or empty mesh";
            }
          }
          RaiseParamEvent(PI_Status, FF_EVENT_FLAG_VALUE);
        }
      }
    }

    SyncRendererParams();

    // 基础 3D 渲染状态：开启深度测试，关闭背面剔除，提高立体感
    GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);

    renderer.render(viewportW, viewportH);

    // 恢复状态，遵循 FFGL 要求尽量还原默认状态
    if(!depthEnabled) glDisable(GL_DEPTH_TEST);
    if(cullEnabled) glEnable(GL_CULL_FACE);

    return FF_SUCCESS;
  }

  // 参数写入
  FFResult SetFloatParameter(unsigned int index, float value) override {
    switch(index){
      case PI_ModelPath: // 文件参数不通过 float 设置
        return FF_FAIL;
      case PI_ForceBake:
        forceBake = value;
        // 若已有模型路径，切换 Force Bake 后自动重载，便于用户立即看到效果
        if(!loadInProgress && !currentModelPath.empty()){
          StartBackgroundLoad(currentModelPath);
        }
        break;
      case PI_ImportMode:
        importMode = value;
        if(!loadInProgress && !currentModelPath.empty()){
          StartBackgroundLoad(currentModelPath);
        }
        break;
      
      case PI_AnimOn: animOn = value; break;
      case PI_AnimSpeed: animSpeed = value; break;
      case PI_AnimTime: animTime = value; break;
      case PI_Scale: scale = value; break;
      case PI_RotX: rot[0] = value; break;
      case PI_RotY: rot[1] = value; break;
      case PI_RotZ: rot[2] = value; break;
  case PI_BaseH: base[0] = value; break;
  case PI_BaseS: base[1] = value; break;
  case PI_BaseL: base[2] = value; break;
      case PI_PosX: pos[0] = value; break;
      case PI_PosY: pos[1] = value; break;
      case PI_PosZ: pos[2] = value; break;
    case PI_AutoCenter: autoCenter = value; break;
    case PI_NormalizeScale: normalizeScale = value; break;
      case PI_CamRadius: camRadius = value; break;
      case PI_CamYaw: camYaw = value; break;
      case PI_CamPitch: camPitch = value; break;
  case PI_L1Yaw: lYaw[0]=value; break; case PI_L1Pitch: lPitch[0]=value; break;
  case PI_L1H: lColor[0][0]=value; break; case PI_L1S: lColor[0][1]=value; break; case PI_L1L: lColor[0][2]=value; break;
      case PI_L1Size: lSize[0]=value; break;
  case PI_L2Yaw: lYaw[1]=value; break; case PI_L2Pitch: lPitch[1]=value; break;
  case PI_L2H: lColor[1][0]=value; break; case PI_L2S: lColor[1][1]=value; break; case PI_L2L: lColor[1][2]=value; break;
      case PI_L2Size: lSize[1]=value; break;
  case PI_L3Yaw: lYaw[2]=value; break; case PI_L3Pitch: lPitch[2]=value; break;
  case PI_L3H: lColor[2][0]=value; break; case PI_L3S: lColor[2][1]=value; break; case PI_L3L: lColor[2][2]=value; break;
      case PI_L3Size: lSize[2]=value; break;
      // Effects
    case PI_DispAmt: dispAmt = value; break; case PI_DispFreq: dispFreq = value; break;
    case PI_ExplodeAmt: explodeAmt = value; break; case PI_CellSize: cellSize = value; break; case PI_Seed: seed = value; break;
    case PI_ExplodeTime: explodeTime = value; break; case PI_Gravity: gravity = value; break; case PI_Spin: spin = value; break;
    case PI_TwistAmt: twistAmt = value; break; case PI_StretchX: stretch[0] = value; break; case PI_StretchY: stretch[1] = value; break; case PI_StretchZ: stretch[2] = value; break;
      case PI_VoxelSize: voxelSize = value; break;
  case PI_SketchOn: sketchOn = value; break;
  case PI_WireThick: wireThick = value; break;
  case PI_WireH: wireCol[0] = value; break; case PI_WireS: wireCol[1] = value; break; case PI_WireL: wireCol[2] = value; break;
      case PI_GridSize: gridSize = value; break;
      case PI_Decimate: decimate = value; break;
      default:
        return FF_FAIL;
    }
    return FF_SUCCESS;
  }

  // 文本/文件参数写入
  FFResult SetTextParameter(unsigned int index, const char* value) override {
    if(index == PI_ModelPath){
      if(value && value[0] != '\0'){
        // 启动后台解析（非 GL 线程），完成后在 GL 线程上传
        StartBackgroundLoad(value);
        return FF_SUCCESS;
      }
      return FF_FAIL;
    }
    if(index == PI_Status){
      // 只读：忽略外部写入
      return FF_SUCCESS;
    }
    return FF_FAIL;
  }

  float GetFloatParameter(unsigned int index) override {
    switch(index){
      case PI_ModelPath: return 0.0f; // 文件参数不走 float
      case PI_ForceBake: return forceBake;
      case PI_ImportMode: return importMode;
  
      case PI_AnimOn: return animOn;
      case PI_AnimSpeed: return animSpeed;
      case PI_AnimTime: return animTime;
      case PI_Scale: return scale;
      case PI_RotX: return rot[0];
      case PI_RotY: return rot[1];
      case PI_RotZ: return rot[2];
  case PI_BaseH: return base[0];
  case PI_BaseS: return base[1];
  case PI_BaseL: return base[2];
      case PI_PosX: return pos[0];
      case PI_PosY: return pos[1];
      case PI_PosZ: return pos[2];
      case PI_AutoCenter: return autoCenter;
  case PI_NormalizeScale: return normalizeScale;
      case PI_CamRadius: return camRadius;
      case PI_CamYaw: return camYaw;
      case PI_CamPitch: return camPitch;
      case PI_L1Yaw: return lYaw[0];
      case PI_L1Pitch: return lPitch[0];
  case PI_L1H: return lColor[0][0];
  case PI_L1S: return lColor[0][1];
  case PI_L1L: return lColor[0][2];
      case PI_L1Size: return lSize[0];
      case PI_L2Yaw: return lYaw[1];
      case PI_L2Pitch: return lPitch[1];
  case PI_L2H: return lColor[1][0];
  case PI_L2S: return lColor[1][1];
  case PI_L2L: return lColor[1][2];
      case PI_L2Size: return lSize[1];
      case PI_L3Yaw: return lYaw[2];
      case PI_L3Pitch: return lPitch[2];
  case PI_L3H: return lColor[2][0];
  case PI_L3S: return lColor[2][1];
  case PI_L3L: return lColor[2][2];
      case PI_L3Size: return lSize[2];
    case PI_DispAmt: return dispAmt; case PI_DispFreq: return dispFreq;
    case PI_ExplodeAmt: return explodeAmt; case PI_CellSize: return cellSize; case PI_Seed: return seed;
    case PI_ExplodeTime: return explodeTime; case PI_Gravity: return gravity; case PI_Spin: return spin;
    case PI_TwistAmt: return twistAmt; case PI_StretchX: return stretch[0]; case PI_StretchY: return stretch[1]; case PI_StretchZ: return stretch[2];
      case PI_VoxelSize: return voxelSize;
  case PI_SketchOn: return sketchOn;
  case PI_WireThick: return wireThick;
  case PI_WireH: return wireCol[0]; case PI_WireS: return wireCol[1]; case PI_WireL: return wireCol[2];
      case PI_GridSize: return gridSize;
      case PI_Decimate: return decimate;
      default: return 0.0f;
    }
  }

  // 供主机显示文件路径
  char* GetTextParameter(unsigned int index) override {
    if(index == PI_ModelPath){
      // 返回当前已加载（或默认）的模型路径
      // 注意：主机会立即读取，此处返回的指针在下次调用前有效
      if(currentModelPath.empty()){
        static std::string emptyStr;
        emptyStr.clear();
        return emptyStr.data();
      }
      return const_cast<char*>(currentModelPath.c_str());
    }
    if(index == PI_Status){
      return const_cast<char*>(lastStatus.c_str());
    }
    return CFFGLPlugin::GetTextParameter(index);
  }

  // 注：已去除 Reload 事件参数；重新加载请通过修改文件路径触发

private:
  // 启动后台 OBJ 解析
  void StartBackgroundLoad(const std::string& path){
    if(loadInProgress) return;
    loadInProgress = true;
    lastRequestedModelPath = path;
    timedOutNotice = false;
    importStart = std::chrono::steady_clock::now();
    if(loaderThread.joinable()){
      // 不等待旧线程，避免阻塞
      try{ loaderThread.detach(); }catch(...){ }
    }
    // 捕获当前 ForceBake 状态，保证此次加载使用同一策略
    // 导入模式映射
    // modeStr 仅用于 Status 提示
    auto modeFromValue = [](float v)->int{
      if(v < 0.34f) return 0; // Auto
      if(v < 0.67f) return 1; // SkipFallbacks
      return 2;               // StaticBake
    };
    int mode = modeFromValue(importMode);
    const char* modeStr = (mode==0?"Auto":(mode==1?"SkipFallbacks":"StaticBake"));
    bool useForceBake = (forceBake > 0.5f) || (mode==2);
    bool disableFallbacks = (mode==1);
    loaderThread = std::thread([this, path, useForceBake, disableFallbacks, modeStr]{
      {
        std::lock_guard<std::mutex> lk(meshMutex);
        lastStatus = std::string("Loading: ") + path + " (mode=" + modeStr + ")";
      }
      RaiseParamEvent(PI_Status, FF_EVENT_FLAG_VALUE);
      std::string log;
      auto mesh = std::make_unique<MeshData>();
      bool ok = false;
      std::string ext;
      if(const char* dot = strrchr(path.c_str(),'.')){ ext = std::string(dot+1); }
      for(char& c: ext) c = (char)std::tolower((unsigned char)c);
      if(ext=="obj"){
        ok = OBJLoader::load(path, *mesh, log);
      }else if(ext=="fbx" || ext=="gltf" || ext=="glb"){
        ok = FBXLoader::load(path, *mesh, log, useForceBake, disableFallbacks);
      }else{
        log = "不支持的模型格式: ." + ext + " (支持: .obj/.fbx/.gltf/.glb)";
        ok = false;
      }
  if(shuttingDown) { loadInProgress = false; return; }
  if (ok)
      {
        bool skinned = mesh->skinned;
        size_t boneCount = mesh->bones.size();
        size_t chanCount = mesh->channels.size();
        double duration = mesh->animDuration;
        double tps = mesh->animTicksPerSecond;
        size_t vcount = mesh->vertices.size() / 16; // 按统一 16-float 布局
        size_t icount = mesh->indices.size();
        char msg[256];
        std::snprintf(msg, sizeof(msg),
          "OK: vertices=%zu, indices=%zu, skinned=%s, bones=%zu, channels=%zu, anim=(duration=%.3f, tps=%.3f)",
          vcount, icount, skinned?"true":"false", boneCount, chanCount, duration, tps);
        FFGLLog::LogToHost(msg);
        {
          std::lock_guard<std::mutex> lk(meshMutex);
          lastStatus = std::string(msg) + (log.empty()?"":std::string(" | ")+log) + std::string(" | import=") + modeStr;
        }
        RaiseParamEvent(PI_Status, FF_EVENT_FLAG_VALUE);
        // 仅当解析成功且有三角形时才进入上传队列
        if(icount > 0){
          std::lock_guard<std::mutex> lk(meshMutex);
          pendingMesh = std::move(mesh);
          meshReady = true;
        } else {
          // 没有三角形可绘制，视为失败
          FFGLLog::LogToHost("No indices generated; skipping GPU upload");
          {
            std::lock_guard<std::mutex> lk(meshMutex);
            lastStatus = std::string("ERROR: No renderable triangles (indices=0)");
          }
          RaiseParamEvent(PI_Status, FF_EVENT_FLAG_VALUE);
        }
      } else {
        std::string msg = std::string("ERROR: ") + log + std::string(" | import=") + modeStr;
        FFGLLog::LogToHost(msg.c_str());
        {
          std::lock_guard<std::mutex> lk(meshMutex);
          lastStatus = msg;
        }
        RaiseParamEvent(PI_Status, FF_EVENT_FLAG_VALUE);
        // 解析失败，不入队上传
        {
          std::lock_guard<std::mutex> lk(meshMutex);
          pendingMesh.reset();
          meshReady = false;
        }
      }
      loadInProgress = false;
    });
  }

  // HSL -> RGB 辅助
  static float hue2rgb(float p, float q, float t){
    if(t < 0.0f) t += 1.0f;
    if(t > 1.0f) t -= 1.0f;
    if(t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
    if(t < 1.0f/2.0f) return q;
    if(t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
    return p;
  }
  static void HSLtoRGB(float h, float s, float l, float out[3]){
    h = fmodf(h, 1.0f); if(h < 0) h += 1.0f;
    if(s <= 0.00001f){ out[0]=out[1]=out[2]=l; return; }
    float q = (l < 0.5f) ? (l * (1.0f + s)) : (l + s - l*s);
    float p = 2.0f * l - q;
    out[0] = hue2rgb(p,q,h + 1.0f/3.0f);
    out[1] = hue2rgb(p,q,h);
    out[2] = hue2rgb(p,q,h - 1.0f/3.0f);
  }

  void SyncRendererParams(){
    // 旋转映射到 0..360 度
    float rotDeg[3] = { rot[0]*360.0f, rot[1]*360.0f, rot[2]*360.0f };
  float defaultLight[3] = {0.3f, 0.7f, 0.6f};
  // 基色：HSL 控件 → 转为 RGB 上传
  float baseRGB[3]; HSLtoRGB(base[0], base[1], base[2], baseRGB);
  renderer.setParams(scale, rotDeg, baseRGB, defaultLight);
  // 位置范围扩大：0..1 → -10..10（每轴扩大约10倍）
  const float posRange = 20.0f; // 输出全幅
  const float posHalf  = posRange * 0.5f;
  float p[3] = { pos[0]*posRange - posHalf, pos[1]*posRange - posHalf, pos[2]*posRange - posHalf };
    renderer.setPosition(p);
    renderer.setAutoCenter(autoCenter > 0.5f);
    renderer.setNormalizeScale(normalizeScale > 0.5f);
    // 相机轨道参数：半径 0..1 → [1..10]；Yaw 0..1 → 0..360°；Pitch 0..1 → [-89..89]°
    float radius = 1.0f + camRadius * 9.0f;
    float yawDeg = camYaw * 360.0f;
    float pitchDeg = -89.0f + camPitch * 178.0f;
    renderer.setCameraOrbit(radius, yawDeg, pitchDeg);
    // 三盏灯：Yaw/Pitch、颜色、光斑大小映射
    for(int i=0;i<3;++i){
      float lyawDeg = (i==0? lYaw[0] : (i==1? lYaw[1] : lYaw[2])) * 360.0f;
      float lpitchDeg = -89.0f + (i==0? lPitch[0] : (i==1? lPitch[1] : lPitch[2])) * 178.0f;
      renderer.setLightOrbit(i, lyawDeg, lpitchDeg);
  // 灯光颜色：HSL -> RGB
  float lc[3]; HSLtoRGB(lColor[i][0], lColor[i][1], lColor[i][2], lc);
  renderer.setLightColor(i, lc);
      // 0..1 → [8..128] 作为 shininess（值越大高光越小越锐利）
      float shine = 8.0f + (lSize[i] * 120.0f);
      renderer.setLightShine(i, shine);
    }
    // 效果参数映射
    // Noise Displace（幅度 0 关闭）
    float dispA = dispAmt * 1.5f; // 0..1.5 单位
    float dispF = 0.1f + dispFreq * 9.9f; // 0.1..10
    renderer.setNoiseDisplace(dispA, dispF);
    // Explode
  float expA = explodeAmt * 20.0f; // 回退：上限恢复为 0..20 单位
  float cell = 0.01f + cellSize * 0.99f; // 0.01..1.0 更小的碎片
    float sd = seed * 10.0f; // 0..10
    renderer.setExplode(expA, cell, sd);
  float t = explodeTime * 1.0f; // 大幅减小时间尺度：以 0..1 为主，速度主要由 Amount 决定
  float g = gravity * 10.0f; // 0..10 重力强度
  float sp = spin * 1.0f;    // 0..1 转/进度（在 shader 内部再乘 2π）
    renderer.setExplodeDynamics(t, g, sp);
    // Twist（-6..6 弧度/单位）
    float tw = -6.0f + twistAmt * 12.0f;
    renderer.setTwist(tw);
    // Stretch（0.1..1.9，0.5 -> 1.0 中性）
    auto mapStretch = [](float v){ return (v <= 0.0f) ? 0.1f : (v >= 1.0f ? 1.9f : (0.1f + 1.8f * v)); };
    float sx = mapStretch(stretch[0]);
    float sy = mapStretch(stretch[1]);
    float sz = mapStretch(stretch[2]);
    renderer.setStretch(sx, sy, sz);
    // Voxel
    float vox = (voxelSize <= 1e-6f) ? 0.0f : (0.02f + voxelSize*0.48f); // 0 表示关闭
    renderer.setVoxel(vox);
    // Wire
    float wth = wireThick; // 0 关闭
  float wcol[3]; HSLtoRGB(wireCol[0], wireCol[1], wireCol[2], wcol);
    float gr = 0.02f + gridSize*0.48f;
    renderer.setWire(wth, wcol, gr);
  renderer.setSketchOn(sketchOn > 0.5f);
    // Decimate
    renderer.setDecimate(decimate);
    // 动画参数同步
    bool enabled = (animOn > 0.5f);
    float speedMul = 2.0f * std::max(0.0f, std::min(1.0f, animSpeed)); // 0..1 -> 0..2
    float t01 = std::max(0.0f, std::min(1.0f, animTime));
    renderer.setAnimParams(enabled, speedMul, t01);
    
  }

  ModelRenderer renderer;
  int viewportW, viewportH;
  float scale, rot[3], base[3];
  float pos[3];
  float autoCenter;
  float normalizeScale;
  float camRadius, camYaw, camPitch;
  // 三盏灯的参数（0..1 空间）
  float lYaw[3];
  float lPitch[3];
  float lColor[3][3];
  float lSize[3];
  // Effects state（0..1 空间）
  float dispAmt, dispFreq;
  float explodeAmt, cellSize, seed;
  float explodeTime, gravity, spin;
  float twistAmt;
  float stretch[3];
  float voxelSize;
  float sketchOn;
  float wireThick, wireCol[3], gridSize;
  float decimate;
  float forceBake;
  float importMode;
  // 异步导入控制/看门狗
  std::atomic<bool> shuttingDown{false};
  std::atomic<bool> timedOutNotice{false};
  std::chrono::steady_clock::time_point importStart{};
  const int importTimeoutMs = 8000; // 超过 8s 给出提示（不强制中断）
  
  // 动画控件（0..1 空间）
  float animOn;
  float animSpeed;
  float animTime;
  std::string currentModelPath;
  // 仅在 GL 线程执行模型加载，避免崩溃
  // 异步加载相关
  std::atomic<bool> loadInProgress;
  std::atomic<bool> meshReady;
  std::mutex meshMutex;
  std::unique_ptr<MeshData> pendingMesh;
  std::thread loaderThread;
  std::string lastRequestedModelPath;
  std::string lastStatus; // 最近一次加载状态/错误摘要（供 Status 文本参数显示）
};

// 插件信息与工厂注册
static CFFGLPluginInfo PluginInfo(
  PluginFactory< FFGLModelFXPlugin >,
  "YT3D",
  "ModelFX 3D",
  2, 1,   // API major, minor (use 2.1 for wider host compatibility)
  1, 1,   // Plugin major, minor (bumped for Import Mode)
  FF_SOURCE,
  "3D model import with effects",
  "YT FFGL Example"
);
// ================== FFGL 粘合层结束 ==================
