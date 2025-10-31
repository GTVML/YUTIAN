#pragma once
#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif
#include "GLShader.h"
#include "OBJLoader.h"

// 说明：由于未直接集成 FFGL SDK，本头文件仅声明插件所需的核心资源与方法。
// 实际 FFGL 基类/参数注册/plugMain 入口在 FFGLModelFX.cpp 中，依赖 FFGL SDK。

class ModelRenderer {
public:
  bool init(const std::string& shaderDir, std::string& log);
  bool loadModel(const std::string& path, std::string& log);
  // 直接上传已解析的网格数据（在 GL 线程调用）
  bool setMesh(const MeshData& mesh);
  // 用于在上传后给 Status 提供动画/骨骼统计信息
  std::string getAnimStatsString() const;
  void setParams(float scale, const float rotDeg[3], const float baseColor[3], const float lightDir[3]);
  void setPosition(const float pos[3]) { position_[0]=pos[0]; position_[1]=pos[1]; position_[2]=pos[2]; }
  void setCameraOrbit(float radius, float yawDeg, float pitchDeg) { camRadius_=radius; camYawDeg_=yawDeg; camPitchDeg_=pitchDeg; }
  void setLightOrbit(int idx, float yawDeg, float pitchDeg) {
    if(idx<0||idx>=3) return; lightYawDeg_[idx]=yawDeg; lightPitchDeg_[idx]=pitchDeg; }
  void setLightColor(int idx, const float color[3]) {
    if(idx<0||idx>=3) return; lightColor_[idx][0]=color[0]; lightColor_[idx][1]=color[1]; lightColor_[idx][2]=color[2]; }
  void setLightShine(int idx, float shine) { if(idx<0||idx>=3) return; lightShine_[idx]=shine; }
  void setAutoCenter(bool enabled) { autoCenter_ = enabled; }
  void setNormalizeScale(bool enabled) { normalize_ = enabled; }
  // 效果参数设置
  void setNoiseDisplace(float amt, float freq){ dispAmt_=amt; dispFreq_=freq; }
  void setExplode(float amt, float cell, float seed){ explodeAmt_=amt; cellSize_=cell; seed_=seed; }
  void setExplodeDynamics(float time01, float gravity, float spin){ explodeT_=time01; gravity_=gravity; spin_=spin; }
  void setTwist(float twist){ twistAmt_=twist; }
  void setStretch(float sx,float sy,float sz){ stretch_[0]=sx; stretch_[1]=sy; stretch_[2]=sz; }
  void setVoxel(float size){ voxelSize_=size; }
  void setWire(float thick, const float color[3], float grid){ wireThick_=thick; wireColor_[0]=color[0]; wireColor_[1]=color[1]; wireColor_[2]=color[2]; gridSize_=grid; }
  void setDecimate(float d){ decimate_=d; }
  void setSketchOn(bool on){ sketchOn_ = on; }
  // 动画控制：enabled=true 按速度播放；enabled=false 使用 time01 手动 scrub
  void setAnimParams(bool enabled, float speedMul, float time01){ animEnabled_=enabled; animSpeed_=speedMul; animTime01_=time01; }
  void render(int vpWidth, int vpHeight);
  void release();

private:
  static constexpr int MAX_BONES = 60; // 避免超过 Mac GL uniform vec 限制
  GLuint vao_=0, vbo_=0, ebo_=0;
  size_t indexCount_=0;
  GLShader shader_;
  bool skinned_ = false;
  std::vector<MeshData::Bone> bones_;
  std::vector<MeshData::Channel> channels_;
  double animDuration_ = 0.0;
  double animTicksPerSecond_ = 25.0;
  // 记录压缩前的原始骨骼/通道数量，便于诊断
  size_t originalBoneCount_ = 0;
  size_t originalChannelCount_ = 0;
  size_t weightedVertexCount_ = 0; // 权重大于 0 的顶点数量（诊断用）
  std::vector<float> bonePalette_; // 16*MAX_BONES floats (列主序)

  float scale_=1.0f;
  float rot_[3] = {0,0,0};
  float baseColor_[3] = {1,1,1};
  bool autoCenter_ = false;
  bool normalize_ = false;
  float normalizeFactor_ = 1.0f; // 由 AABB 最大轴计算得到

  // 模型中心（用于围绕自身中心旋转/缩放）
  float center_[3] = {0.f, 0.f, 0.f};
  // 模型世界平移
  float position_[3] = {0.f, 0.f, 0.f};
  // 相机绕模型的轨道参数
  float camRadius_ = 3.0f; // 距离
  float camYawDeg_ = 0.0f; // 水平角（绕 Y）
  float camPitchDeg_ = 0.0f; // 俯仰（绕 X）
  // 三盏方向光：轨道角、颜色、光斑（高光）大小
  float lightYawDeg_[3]   = {45.0f, 180.0f, 315.0f};
  float lightPitchDeg_[3] = {30.0f, 30.0f, 30.0f};
  float lightColor_[3][3] = { {1,1,1}, {1,1,1}, {1,1,1} };
  float lightShine_[3]    = {32.0f, 32.0f, 32.0f};

  // Effect params
  float dispAmt_ = 0.0f, dispFreq_ = 1.0f;
  float explodeAmt_ = 0.0f, cellSize_ = 0.2f, seed_ = 0.0f;
  float explodeT_ = 0.0f, gravity_ = 0.0f, spin_ = 0.0f;
  float twistAmt_ = 0.0f, stretch_[3] = {1.0f,1.0f,1.0f};
  float voxelSize_ = 0.2f;
  float wireThick_ = 0.2f, wireColor_[3] = {1,1,1}, gridSize_ = 0.2f;
  float decimate_ = 0.0f;
  bool sketchOn_ = false;

  // 动画控制
  bool  animEnabled_ = true;   // 是否播放
  float animSpeed_   = 1.0f;   // 速度倍数（1.0 正常速度）
  float animTime01_  = 0.0f;   // 手动时间（0..1）

  // 连续时间（用于连续自转）
  std::chrono::steady_clock::time_point startTime_;

  void ensureBuffers(const MeshData& mesh);
  void uploadBones(float animTimeSec);
};