#pragma once
#include <string>
#include "OBJLoader.h" // MeshData

class FBXLoader {
public:
  // 使用 Assimp 读取 FBX（含二进制/ASCII），输出为三角网格的 interleaved 顶点与索引。
  // 仅导入静态姿态（将层级矩阵预变换进顶点）。动画/蒙皮在后续阶段实现。
    // 通过 Assimp 载入模型（FBX/glTF/等），输出统一 16-float 布局顶点
    // forceBake: 若为 true，则直接尝试 PreTransformVertices 静态烘焙（禁用动画/蒙皮）
  // disableFallbacks: 若为 true，直接读取失败时不进行内存导入/预变换/ASCII 解析等回退
  static bool load(const std::string& path, MeshData& out, std::string& log, bool forceBake=false, bool disableFallbacks=false);
};
