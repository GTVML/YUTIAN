#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vObjPos;

out vec4 FragColor;

uniform vec3 uBaseColor = vec3(1.0, 1.0, 1.0);
uniform vec3 uEyePos; // 世界空间摄像机位置

// 三盏可配置的方向光
uniform vec3 uLightDir[3];    // 方向（已归一化）
uniform vec3 uLightColor[3];  // 颜色/强度
uniform float uLightShine[3]; // 高光“光斑大小”（越大高光越小、越锐利）

// Effect (simultaneous)
uniform float uWireThick; // 0..1 相对厚度
uniform vec3 uWireColor;
uniform float uDecimate; // 0..1 概率阈值
uniform float uGridSize; // 线框栅格尺寸（世界空间）
uniform float uSketchOn; // >0.5 开启线框叠加

float hash(vec3 p){
  p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
  p *= 17.0;
  return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

void main(){
  // Dissolve/Decimate：基于世界坐标的随机丢弃（始终可用）
  {
    float r = hash(floor(vWorldPos));
    if(r < clamp(uDecimate, 0.0, 1.0)) discard;
  }

  vec3 N = normalize(vNormal);
  vec3 V = normalize(uEyePos - vWorldPos);
  vec3 albedo = uBaseColor;
  vec3 ambient = 0.2 * albedo;
  vec3 color = ambient;

  for(int i=0;i<3;++i){
    vec3 L = normalize(uLightDir[i]);
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = NdotL * albedo * uLightColor[i];
    // Blinn-Phong 半程向量
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float shininess = max(uLightShine[i], 1.0);
    float spec = pow(NdotH, shininess);
    vec3 specular = spec * uLightColor[i];
    color += diffuse + specular * 0.5; // 适度的高光权重
  }

  // Wireframe Overlay：基于当前世界坐标叠加线框（回退为 vWorldPos）
  if(uSketchOn > 0.5){
    float g = max(uGridSize, 1e-4);
    vec3 grid = abs(fract(vWorldPos / g) - 0.5);
    float line = min(min(grid.x, grid.y), grid.z);
    // 使用 fwidth 做抗锯齿
    float w = mix(0.001, 0.02, clamp(uWireThick, 0.0, 1.0));
    float aa = fwidth(line) * 1.5;
    float edge = 1.0 - smoothstep(w, w+aa, line);
    color = mix(color, uWireColor, clamp(edge, 0.0, 1.0));
  }

  FragColor = vec4(color, 1.0);
}
