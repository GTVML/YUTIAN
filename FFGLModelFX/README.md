# FFGLModelFX — 3D 模型导入与效果（FFGL 插件骨架）

## 快速部署（macOS）

以下步骤将把构建好的 `ModelFXFFGL.bundle` 安装到常见宿主会扫描的 FFGL 插件目录，并确保宿主加载到最新版本（1.1）。

1) 关闭宿主应用（例如 Resolume Arena、VDMX）

2) 复制 Bundle 到插件目录（两处皆可被多数宿主识别）

```bash
# 构建产物位置（本仓库已提供 Make 构建目录）
open "FFGLModelFX/build-make-release"

# 推荐安装位置 A（Resolume 习惯路径）
mkdir -p "$HOME/Documents/Resolume/Extra Effects"
cp -R "FFGLModelFX/build-make-release/ModelFXFFGL.bundle" "$HOME/Documents/Resolume/Extra Effects/"

# 推荐安装位置 B（FFGL 标准用户路径）
mkdir -p "$HOME/Library/Graphics/FreeFrame Plug-Ins"
cp -R "FFGLModelFX/build-make-release/ModelFXFFGL.bundle" "$HOME/Library/Graphics/FreeFrame Plug-Ins/"

# 移除隔离属性（避免“来自互联网”导致加载受限）
xattr -dr com.apple.quarantine "$HOME/Documents/Resolume/Extra Effects/ModelFXFFGL.bundle" 2>/dev/null || true
xattr -dr com.apple.quarantine "$HOME/Library/Graphics/FreeFrame Plug-Ins/ModelFXFFGL.bundle" 2>/dev/null || true
```

3) 重启宿主并验证

- 在插件 UI 的 Status 文本中应看到 `import=Auto|SkipFallbacks|StaticBake` 字样（用于确认加载的是新版本 1.1）。
- 若要避免“长时间 Loading…”，将 Import Mode 切换为 `SkipFallbacks`，加载有问题的 glb/fbx 会快速失败并显示明确错误文字，便于排查。
- 若仍然卡住，重启宿主应能正常退出（插件已改为在卸载时分离加载线程，避免阻塞）。

4) 一键脚本（可选）

本仓库提供脚本用于自动备份旧版本并部署到上述两个目录：

```bash
bash FFGLModelFX/scripts/deploy.sh
```

脚本会：

- 备份已存在的 `ModelFXFFGL.bundle` 到同目录并添加时间戳后缀。
- 复制新的 bundle 并移除 `com.apple.quarantine` 属性。
- 输出部署结果与下一步提示。

如需自定义目标目录：

```bash
bash FFGLModelFX/scripts/deploy.sh --dest "$HOME/Documents/Resolume/Extra Effects"
```

出现“宿主没有识别到插件”请参考下文“故障排查”。

本工程提供一个基于 FFGL 2.x SDK 的插件骨架，用于：
- 加载 OBJ 格式 3D 模型（顶点/法线/UV，三角面）
- 在 OpenGL 中渲染模型
- 应用基础着色效果（环境光/漫反射/轮廓/卡通/颜色偏移/噪声位移）

由于当前工作区没有 FFGL SDK，本仓库提供可直接落地的工程结构与代码骨架；你只需配置 FFGL SDK 路径并编译即可。下文包含构建步骤与可选回退方案。

## 目录结构

- `CMakeLists.txt`：CMake 构建文件（macOS 生成 .bundle）
- `resources/Info.plist.in`：Bundle Info 模板
- `resources/shaders/`：GLSL 着色器
- `src/`：源码（OBJ 加载、着色器、插件粘合层）
- `README.md`：本文档

## 依赖

- macOS（已安装 Xcode 命令行工具）
- FFGL 2.x SDK（Resolume Git 开源）
  - 本工程已内置 Git 拉取方案：若未设置 `FFGL_SDK_DIR`，CMake 会自动从 `https://github.com/resolume/ffgl` 抓取 SDK（默认 `master`）。
  - 如你想固定版本，可在配置时指定 `-DFFGL_GIT_TAG=v2.2`（或其他 tag）。
  - 如你已有本地 SDK，设置 `FFGL_SDK_DIR` 将优先使用本地路径。

可选：如暂未配置 SDK，你仍可先阅读/修改 `src` 与 `resources/shaders`，待配置 SDK 后再编译。

## 构建（CMake + Xcode）

1. 在 shell 中设置 SDK 路径（或在 CMake GUI 中设置）。
2. 生成 Xcode 工程并构建。

示例命令（可选运行）：

```bash
# 配置你的 FFGL SDK 路径
export FFGL_SDK_DIR="/path/to/ffgl/sdk"

# 生成 Xcode 工程
cmake -S . -B build -G Xcode -DFFGL_SDK_DIR="$FFGL_SDK_DIR"

# 构建 Release
cmake --build build --config Release
```

构建成功后，将生成 `.bundle`，可复制到宿主（如 Resolume）插件目录测试。

提示（macOS 通用二进制）：Resolume 7.11+ 支持 ARM 原生；建议在 Xcode 选择 “Any Mac (Apple Silicon, Intel)” 以生成 Universal 二进制。

## 用 Make 部署（不使用 Xcode）

方式 A：手动复制

```bash
# 生成 Unix Makefiles（Release）并自动拉取 FFGL SDK（需联网）
cmake -S . -B build-make-release -G "Unix Makefiles" -DFFGL_GIT_TAG=master -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build-make-release --parallel

# 产物位置
# build-make-release/ModelFXFFGL.bundle

# 手动复制到 Resolume 插件目录
mkdir -p "$HOME/Documents/Resolume/Extra Effects"
cp -R "build-make-release/ModelFXFFGL.bundle" "$HOME/Documents/Resolume/Extra Effects/"
```

方式 B：使用自带 deploy 目标

```bash
# 配置（同上）
cmake -S . -B build-make-release -G "Unix Makefiles" -DFFGL_GIT_TAG=master -DCMAKE_BUILD_TYPE=Release

# 构建 + 部署（会复制到 ~/Documents/Resolume/Extra Effects/）
cmake --build build-make-release --target deploy --parallel
```

方式 C：构建后自动部署

```bash
# 配置时打开自动部署开关（构建完成后会自动复制）
cmake -S . -B build-make-release -G "Unix Makefiles" -DFFGL_GIT_TAG=master -DCMAKE_BUILD_TYPE=Release -DDEPLOY_TO_RESOLUME=ON

# 编译
cmake --build build-make-release --parallel
```

可选变量：
- 指定 Resolume 插件目录：`-DRESOLUME_EXTRA_EFFECTS_DIR="/your/path"`
- 使用本地 SDK：`-DFFGL_SDK_DIR=/path/to/ffgl`（优先于 Git 拉取）

构建完成后，在 Resolume 的 Sources/Effects 里找到 “ModelFX 3D”。

## 模型导入方式

- 参数 `Model`（文件选择，.obj）：
  - 宿主将弹出文件对话框，选择 OBJ 后自动触发异步加载（在 GL 线程执行，安全不闪退）。
  - 当前版本仅支持 OBJ；若模型缺少法线，插件会自动生成平滑法线。
- 回退方案：
  - 将模型放到 `~/Documents/FFGLModelFX/model.obj`，插件会在启动时自动查找并加载；或
  - 修改 `src/FFGLModelFX.cpp` 中的默认路径逻辑（DefaultModelPath / GetBundleDefaultModel）。

注意：OBJ 支持 v/vt/vn/f（仅三角形）。如有四边面，需事先三角化。

## 参数（当前实现）

- Model（file/.obj）—— 模型文件选择器（支持弹窗）
- Scale（float, 0..1）—— 缩放
- RotX/RotY/RotZ（float, 0..1 → 0..360°）—— 旋转
- PivotX/Y/Z（float）—— 旋转枢轴偏移（基于 AABB 中心）
- BaseR/G/B（color channels）—— 基色
- LightX/Y/Z（float3）—— 光照方向
- EffectMode（int/option）—— 0..3
- EffectIntensity（float）—— 效果强度
- NoiseIntensity（float）—— 顶点噪声强度
- Reload（event）—— 重新加载当前（或默认）模型

注意：部分宿主不支持字符串/文件参数时，可使用回退路径方式。

### 爆炸（Explode）参数与范围（当前版本）

- Explode Amount：0..1（归一化滑块）→ 0..20.0（内部单位，已大幅提升上限）
  - 决定碎片“初速度幅度”的主因子；建议与 Cell Size、Gravity 配合微调。
- Explode Time：0..1（归一化滑块）→ 0..1（内部时间标度，已大幅降低时间尺度）
  - 更强调用 Amount 控制飞散强度，Time 主要用于“进度/节奏”控制。
- Explode Cell Size：0..1 → 0.01..1.0（更小的最小体素，支持细碎粉化）
- Gravity：0..1 → 0..10（下落加速度幅度）
- Spin：0..1（每单位进度最多 1 圈；更易控，避免过猛旋转）

默认值均为“无效果”中性状态（例如：Amount=0、Time=0、Gravity=0、Spin=0、Cell Size=0 表示关闭体素化）。

## 代码概览

- `src/OBJLoader.*`：简易 OBJ 解析器（无依赖）
- `src/GLShader.*`：GLSL 编译/链接/Uniform 设置
- `src/FFGLModelFX.*`：插件主体（参数、上下文、渲染，依赖 FFGL SDK）

## 已知限制与后续计划

- 暂未集成 GLTF/FBX，后续可用 tinygltf / Assimp。
- 着色器为基础版，可扩展 PBR、阴影、法线/粗糙度贴图。
- 支持热加载：监听模型/着色器文件变更，自动重新加载。
- Windows 版本可通过同一 CMake 方案生成 .dll（需配置 Windows 工具链与 SDK）。

## 备选方案：基于 Vuo 的 FFGL

你提供的 `00_ 3 D_efx.bundle` 使用了 `VuoRunner.framework`。如果你更倾向于 Vuo 流程，可在 Vuo 内部“导出为 FFGL 插件”，并在 Composition 中加入 3D 模型加载与效果节点，然后导出。该方法更依赖 Vuo 生态，不在本工程展开实现。

## 故障排查

- Arena 无法识别/不显示插件：
  1) 路径确认：
     - `~/Documents/Resolume/Extra Effects/ModelFXFFGL.bundle`
  2) Info.plist 关键项：
     - `CFBundleExecutable` 应为 `ModelFXFFGL`
     - `CFBundleIdentifier` 为 `com.yt.ffgl.modelfx`
     - 如为空，重新构建部署（本工程已修复非 Xcode 生成器的 Info.plist 填充）。
  3) 架构匹配（常见原因）：
     - 查看插件：
       ```bash
       file ~/Documents/Resolume/Extra\ Effects/ModelFXFFGL.bundle/Contents/MacOS/ModelFXFFGL
       ```
     - 查看 Resolume（示例路径，按你的安装调整）：
       ```bash
       file "/Applications/Resolume Arena.app/Contents/MacOS/Resolume Arena"
       ```
     - 若一个为 arm64、另一个为 x86_64，则会不识别。
     - 解决：按目标架构单独构建并部署：
       ```bash
       # 仅 Intel（x86_64）
       cmake -S . -B build-intel -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64
       cmake --build build-intel --parallel
       cmake --build build-intel --target deploy --parallel
       ```
       ```bash
       # 仅 Apple Silicon（arm64）
       cmake -S . -B build-arm64 -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
       cmake --build build-arm64 --parallel
       cmake --build build-arm64 --target deploy --parallel
       ```
     - 如需通用二进制（arm64;x86_64），请安装完整 Xcode/CLT 并在配置时设置 `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`（实际可用性取决于本机工具链）。
  4) 插件类型：本插件为 FF_SOURCE，请在 Sources 面板查看。
  5) 首次加载无模型：在 `~/Documents/FFGLModelFX/model.obj` 放一个模型，或把 `model.obj` 放入 bundle 的 `Contents/Resources/`。

- 无法找到 FFGL 头文件/库：检查 `FFGL_SDK_DIR` 是否正确，或在 `CMakeLists.txt` 里直接填写绝对路径。
- 宿主不显示文本参数：使用固定路径回退方案，或将 UI 替换为按钮+预设（选择内置模型）。
- 模型渲染为黑色：检查法线与着色器，或关闭 `EffectMode` 观察基线效果。

## 许可证

本工程仅为模板/骨架代码，按 MIT 许可证分发；如集成第三方库请遵循对应许可。

## 质量门与后续建议

质量门（当前状态）：

- Build：PASS（已通过 CMake 拉取 FFGL SDK，生成 .bundle 并部署）
- Lint/Typecheck：未配置专用规则（可选添加 clang-tidy/clang-format）
- Tests：暂无自动化测试（建议先对 `OBJLoader` 增加最小单元测试）

建议的后续增强：

- 效果升级：法线贴图、粗糙度/金属度、PBR BRDF、阴影/环境光遮蔽
- 资源支持：GLTF/GLB（tinygltf）、FBX（Assimp）
- 热加载：监听模型与着色器文件变化自动重载
- 性能：索引重用、网格合并、Frustum Cull、可选 Instancing
- 兼容：宿主不支持字符串参数时的路径选择器/预设列表