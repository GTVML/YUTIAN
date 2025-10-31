#pragma once
#include <string>
#include <vector>
#include <array>

struct MeshData {
  // 顶点数据：统一使用 16 个 float/顶点的交错布局
  // layout: pos(3), normal(3), uv(2), boneIndices(4 as float), boneWeights(4)
  // 非蒙皮网格时，boneIndices=(0,0,0,0), boneWeights=(1,0,0,0)
  std::vector<float> vertices;
  std::vector<unsigned int> indices;

  

  // 蒙皮/动画数据（仅当 skinned==true 有效）
  bool skinned = false;
  struct Bone {
    std::string name;
    int parent = -1;
    float offset[16]; // aiBone::mOffsetMatrix (列主序)
    float bindLocal[16]; // 该骨骼节点的默认局部变换（来自 aiNode::mTransformation，列主序）
  };
  std::vector<Bone> bones; // 骨骼数组（索引从 0 开始）

  // 动画（仅取第一段动画）
  struct KeyVec3 { double t; float v[3]; };
  struct KeyQuat { double t; float q[4]; };
  struct Channel {
    int boneIndex = -1;
    std::vector<KeyVec3> posKeys;
    std::vector<KeyQuat> rotKeys;
    std::vector<KeyVec3> sclKeys;
  };
  double animDuration = 0.0;        // in ticks
  double animTicksPerSecond = 25.0; // 默认 25，如果文件未提供
  std::vector<Channel> channels;    // 仅包含在 bones 映射内的通道
};

class OBJLoader {
public:
  // 支持 v/vt/vn/f；f 行可为三角、四边形或 n 边形（使用扇形三角化）。
  // 支持负索引（相对索引）。返回是否成功；失败时 log 包含错误信息。
  static bool load(const std::string& path, MeshData& out, std::string& log);
};