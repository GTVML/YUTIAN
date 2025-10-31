#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aBoneIndex; // 以 float 传入，shader 内转 int
layout(location = 4) in vec4 aBoneWeight;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

// Skinning
#define MAX_BONES 60
uniform int uBoneCount;
uniform mat4 uBones[MAX_BONES];

// Effect uniforms (simultaneous)
uniform float uDispAmt, uDispFreq;
uniform float uExplodeAmt, uCellSize, uSeed;
uniform float uExplodeT;       // 0..1 时间进度（仅用于平移推进）
uniform float uGravity;        // 重力强度（单位/进度^2）
uniform float uSpin;           // 碎片自旋速度（圈/秒的归一化因子）
uniform float uTimeSec;        // 连续时间（秒），用于持续自转
uniform float uTwistAmt;
uniform float uStretchX, uStretchY, uStretchZ;
uniform float uVoxelSize;      // 0 表示关闭体素化

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vObjPos; // 物体空间位置，供片元使用

// 简单哈希噪声
float hash(float n){ return fract(sin(n)*43758.5453123); }
float hash(vec3 p){
  p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
  p *= 17.0;
  return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float noise(vec3 p){
  vec3 i = floor(p);
  vec3 f = fract(p);
  f = f*f*(3.0-2.0*f);
  float n = mix(mix(mix(hash(i+vec3(0,0,0)), hash(i+vec3(1,0,0)), f.x),
                   mix(hash(i+vec3(0,1,0)), hash(i+vec3(1,1,0)), f.x), f.y),
               mix(mix(hash(i+vec3(0,0,1)), hash(i+vec3(1,0,1)), f.x),
                   mix(hash(i+vec3(0,1,1)), hash(i+vec3(1,1,1)), f.x), f.y), f.z);
  return n;
}

// 生成哈希 vec3（0..1）
vec3 hash3(vec3 p){
  return vec3(
    hash(p + vec3(0.0, 1.0, 2.0)),
    hash(p + vec3(3.0, 4.0, 5.0)),
    hash(p + vec3(6.0, 7.0, 8.0))
  );
}

// 轴角旋转：绕单位轴 axis 旋转 angle（弧度）
mat3 axisAngleMatrix(vec3 axis, float angle){
  float c = cos(angle), s = sin(angle);
  float t = 1.0 - c;
  vec3 a = normalize(axis);
  float x=a.x, y=a.y, z=a.z;
  return mat3(
    t*x*x + c,     t*x*y - s*z, t*x*z + s*y,
    t*x*y + s*z,   t*y*y + c,   t*y*z - s*x,
    t*x*z - s*y,   t*y*z + s*x, t*z*z + c
  );
}

void main(){
  // GPU Skinning：线性混合蒙皮（非蒙皮模型退化为单元矩阵）
  vec4 skinnedPos = vec4(0.0);
  vec3 skinnedNrm = vec3(0.0);
  float wsum = 0.0;
  for(int i=0;i<4;++i){
    int bi = int(aBoneIndex[i]);
    float w = aBoneWeight[i];
    if(w <= 0.0) continue;
    if(bi < 0 || bi >= uBoneCount) continue;
    mat4 B = uBones[bi];
    skinnedPos += B * vec4(aPos, 1.0) * w;
    skinnedNrm += mat3(B) * aNormal * w;
    wsum += w;
  }
  vec3 pos;
  vec3 nrm;
  if(uBoneCount <= 0 || wsum <= 1e-6){
    // 无蒙皮或该顶点没有任何权重：直接使用原始位置与法线
    pos = aPos;
    nrm = normalize(aNormal);
  } else {
    pos = skinnedPos.xyz;
    nrm = normalize(skinnedNrm);
  }

  // 1) Voxelize（对齐至体素中心，uVoxelSize<=0 时跳过）
  if(uVoxelSize > 0.0){
    float v = max(uVoxelSize, 1e-4);
    pos = floor(pos / v) * v + 0.5*v;
  }
  // 2) Explode（在 Stretch/Twist/Noise 之前执行，回退顺序）
  if(uExplodeAmt > 0.0){
    float cell = max(uCellSize, 1e-4);
    vec3 cid = floor(pos / cell);
    vec3 cellCenter = (cid + 0.5) * cell;
    float rnd = hash(cid + vec3(uSeed));
    float t = max(uExplodeT, 0.0);
    // 爆炸方向与初速度
    vec3 dir = normalize(hash3(cid + vec3(uSeed*0.73)) - 0.5);
    float speed = uExplodeAmt * mix(0.5, 1.0, rnd);
    vec3 v0 = dir * speed;
    vec3 acc = vec3(0.0, -uGravity, 0.0);
  // 自转角度与真实时间相关（恢复连续时间）
    float rnd2 = hash(cid + vec3(uSeed*2.17));
    vec3 axis = normalize(hash3(cid + vec3(uSeed*1.37)) - 0.5);
  float angle = (uSpin * mix(0.5,1.0,rnd2)) * 6.28318530718 * uTimeSec;
  mat3 R = axisAngleMatrix(axis, angle);
    // 围绕分片中心旋转+平移（不再压平到平面）
    vec3 off = pos - cellCenter;
  // 同步旋转法线以体现自转在光照上的变化
  nrm = R * nrm;
  vec3 exploded = cellCenter + R * off + (v0 * t + 0.5 * acc * (t*t));
    pos = exploded;
    // 法线不强制改为碎片平面法线，保持原始法线的变换趋势
  }
  // 3) Stretch（恢复）
  pos *= vec3(max(uStretchX,1e-4), max(uStretchY,1e-4), max(uStretchZ,1e-4));
  // 4) Twist（围绕 Y 轴）
  {
    float ang = uTwistAmt * pos.y;
    float c = cos(ang), s = sin(ang);
    pos.xz = mat2(c,-s,s,c) * pos.xz;
  }
  // 5) Noise Displace（沿法线）
  {
    float n = noise(pos * max(uDispFreq, 0.0001));
    float s = (n*2.0-1.0) * uDispAmt;
    pos += normalize(nrm) * s;
  }

  vec4 world = uModel * vec4(pos, 1.0);
  vWorldPos = world.xyz;
  vNormal = mat3(transpose(inverse(uModel))) * nrm;
  vUV = aUV;
  vObjPos = pos;
  gl_Position = uProj * uView * world;
}
