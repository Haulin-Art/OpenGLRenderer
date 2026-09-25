# OpenGLRenderer

一个从零手写的 OpenGL 渲染引擎（学习项目），目标是把"散落在 main 里的 OpenGL 调用"逐步重构成一套分层的渲染架构。

**技术栈**：C++17 · OpenGL 4.6 Core Profile · GLFW · GLAD · GLM · tinyobjloader

**当前能跑出来的东西**：一个轨道摄像机视角下，猴头模型（不透明、灯在斜上方所以顶部最亮）**把影子投在下方的白色平板上**，外加一片半透明的无限网格地面。

**当前架构状态**：
- 抽象接口 + 具体实现 + 工厂的第一轮解耦已完成 —— `main.cpp` 里**不出现任何 OpenGL 头文件**。
- 后端由编译期宏选择、由唯一的工厂文件装配；`OpenGLRenderer.h` 与 `VulkanRenderer.h` 互不认识。
- **Shadow Pass（单张 1024² 平行光阴影贴图 + 16 点泊松盘 PCF）已接通**。

---

## 目录

- [一、构建与运行](#一构建与运行)
- [二、目录结构](#二目录结构)
- [三、分层与依赖规则](#三分层与依赖规则)
- [四、后端选择机制（宏 + 工厂）](#四后端选择机制宏--工厂)
- [五、Shadow Pass 是怎么接起来的](#五shadow-pass-是怎么接起来的)
- [六、文件清单](#六文件清单)
- [七、文件之间的关系](#七文件之间的关系)
- [八、关键概念](#八关键概念)
- [九、Shader 与顶点格式约定](#九shader-与顶点格式约定)
- [十、已知问题与 TODO](#十已知问题与-todo)
- [十一、变更记录](#十一变更记录)

---

## 一、构建与运行

### 环境要求

| 项 | 要求 |
|---|---|
| CMake | ≥ 3.16 |
| 构建器 | Ninja（本项目用 Ninja 生成器） |
| 编译器 | MinGW-w64（w64devkit）或 MSVC |
| 平台 | Windows 为主；CMakeLists 里也带了 Linux/macOS 分支 |

第三方库**全部已在 `dependencies/` 目录里**，不需要额外安装：

| 库 | 形式 | 用途 |
|---|---|---|
| GLAD | `glad.c` 直接编译进项目（静态库） | 运行时加载 OpenGL 4.6 函数指针 |
| GLFW | 预编译静态库（`lib-mingw-w64/libglfw3.a` 等） | 窗口 / 上下文 / 输入 |
| GLM | 纯头文件 | 数学（vec3 / mat4 / perspective / lookAt / ortho） |
| tinyobjloader | 纯头文件 | 解析 `.obj` 模型 |

### 构建与运行

```bash
cmake -S . -B build
cmake --build build
build/OpenGLRenderer.exe
```

> 程序用编译期宏 `PROJECT_SOURCE_DIR`（由 CMake 通过 `target_compile_definitions` 注入）
> 拼出 shader / obj 文件的**绝对路径**，所以从哪个目录启动都能找到资源。

### 改代码时的四个坑

1. **新增 `.cpp` 必须写进 `CMakeLists.txt` 的 `add_executable`**，否则它不会被编译，调用处会在**链接阶段**报 `undefined reference`。
2. **新增子目录必须加进 `target_include_directories`**，否则该目录下 `#include "xxx.h"` 会报 `No such file or directory`。
   （`src/Core` 就是这么加进去的 —— 当时 `RenderCommand.h` include `"MatrixTools.h"` 报了一堆 `No such file`，根因只有这一个。）
3. **改完 `CMakeLists.txt` 要重新跑一次 `cmake -S . -B build`**（改 CMake 脚本不会自动重配置）。
4. **切换渲染后端要改 `src/config.h` 里的宏**（见 [第四节](#四后端选择机制宏--工厂)），改完同样要重新编译。

### `.gitignore` 的一个坑（已修）

`# Compiled Object files` 里那条 `*.obj` 原本是给 MSVC 目标文件用的，但**Wavefront 的 3D 模型扩展名也是 `.obj`**，会把 `src/mesh/*.obj` 一并忽略。
现在文件末尾的"例外区"里加了 `!src/mesh/*.obj` 放行（同一条规则还救了 GLFW 的 `lib-*` 预编译库）。
**以后新增模型目录，在例外区照抄一行即可。**

---

## 二、目录结构

> 图例：✅ 已实现 · 🚧 有雏形但需要改 · ⬜ 待做（空文件或占位）

```
OpenGLRenderer/
├── CMakeLists.txt                    ✅ 构建脚本（源文件清单 + include 路径 + 第三方库）
├── README.md                         ✅ 本文件
├── .gitignore                        ✅（含"例外区"：GLFW 预编译库 + mesh 模型）
│
├── dependencies/                     ✅ 第三方库（随仓库分发）
│   ├── glad/                         GLAD（include + src/glad.c）
│   ├── glfw/                         GLFW（include + lib-mingw-w64 / lib-vc20xx）
│   ├── glm/                          GLM（header-only）
│   └── tinyobjloader/                tinyobjloader（header-only）
│
└── src/
    ├── main.cpp                      ✅ 程序入口 + 主循环（已不含任何 OpenGL 头）
    ├── config.h                      ✅ 只剩一行后端选择宏
    │
    ├── Core/                         🚧 基础层
    │   ├── MatrixTools.h             🚧 Transform + 矩阵工具（其中两个函数已成死代码）
    │   └── 想法.md                   ✅ 随手记的疑问（沟通用）
    │
    ├── Render/                       🚧 渲染核心层（与图形 API 无关）
    │   ├── Renderer.h                ✅ IRenderer 抽象接口 + CreateRenderer 声明
    │   ├── RendererFactory.h         ✅ 工厂私有头：按宏 include 对应后端
    │   ├── RendererFactory.cpp       ✅ 全项目唯一的"装配点"
    │   ├── RenderCommand.h           ✅ RenderCommand（mesh + material + transform）
    │   ├── RenderQueue.h / .cpp      ✅ 收集 + 排序（不透明在前、透明按距离远→近）
    │   ├── Material.h / .cpp         ✅ 材质：IShader + RenderState
    │   ├── IShader.h                 ✅ 着色器抽象接口（含 2 个通用 uniform setter）
    │   ├── IMesh.h                   ✅ 网格抽象接口
    │   ├── Camera/
    │   │   ├── Camera.h              ✅ CameraData + Camera（轨道相机）
    │   │   └── Camera.cpp
    │   ├── OpenGL/                   ✅ 具体 API 实现（唯一允许出现 gl* / glfw* 的地方）
    │   │   ├── OpenGLRenderer.h/.cpp
    │   │   ├── OpenGLShader.h/.cpp
    │   │   └── OpenGLMesh.h/.cpp
    │   └── Vulkan/
    │       └── VulkanRenderer.h      ⬜ 占位：只有声明，没有 .cpp，未实现
    │
    ├── ObjLoader.h / ObjLoader.cpp   ✅ OBJ 解析 + 顶点展开
    │
    ├── Resources/                    ⬜ 资源管理
    │   └── ResourceManager.h         ⬜ 空文件（规划：加载/缓存 Shader、Mesh、Texture）
    │
    ├── mesh/
    │   ├── monkey.obj / monkey.mtl   ✅ 猴头模型（Suzanne）
    │   └── plane.obj  / plane.mtl    ✅ 单位平面（4 顶点 2 三角形，法线朝上）
    │
    ├── shaders/
    │   ├── basicvertex.glsl          ✅ 猴头/平板用（含 posWS 输出）
    │   ├── basicfrag.glsl            ✅ 半兰伯特 + 阴影（含 PCF 泊松盘）
    │   ├── groundNetVertex.glsl      ✅ 地面网格用
    │   ├── groundNetFrag.glsl        ✅ fwidth 抗锯齿网格（★ 不采样阴影）
    │   ├── shadow_mapping_depth_vert.glsl ✅ 深度专用顶点着色器
    │   └── shadow_mapping_depth_frag.glsl ✅ 深度专用片段着色器（空 main）
    │
    └── 目标渲染架构.md                🚧 架构规划文档（★ 部分内容已过时）
```

**不属于本项目的目录**：`build/`（CMake 生成物，已在 `.gitignore` 里）。

---

## 三、分层与依赖规则

### 3.1 分层

```
        main.cpp                      ← 应用层：主循环、装配调用、收集渲染命令
            │  只认识 IRenderer / IMesh / IShader / Material / Camera / RenderQueue
            │
   ┌────────┴─────────────────────────────────────────┐
   │ Render/           渲染核心层（与图形 API 无关）      │
   │   Renderer.h        IRenderer     后端抽象接口      │
   │   IShader.h         IShader       着色器抽象接口    │
   │   IMesh.h           IMesh         网格抽象接口      │
   │   RenderCommand.h   一条渲染命令                   │
   │   RenderQueue.h     收集 + 排序                    │
   │   Material.h        材质 = IShader + 渲染状态       │
   │   Camera/           摄像机，产出 View / Projection  │
   └────────┬─────────────────────────────────────────┘
            │  （接口层只被"实现"与"工厂"依赖）
   ┌────────┴─────────────────────────────────────────┐
   │ Render/OpenGL/    具体实现（唯一出现 gl* / glfw*）  │
   │   OpenGLRenderer : IRenderer   （含 Shadow Pass）  │
   │   OpenGLShader   : IShader                        │
   │   OpenGLMesh     : IMesh                          │
   └──────────────────────────────────────────────────┘

   Core/MatrixTools.h          ← Transform + 矩阵小工具（被 Render 层使用）
   Render/RendererFactory.cpp  ← 唯一的装配点：知道所有后端，负责 new 出具体实现
```

### 3.2 四条硬规则

| 规则 | 说明 | 现状 |
|---|---|---|
| ① **只有 `Render/OpenGL/` 里可以出现 `gl*` / `glfw*`** | 其它层一律不碰图形 API | ✅ 已达成 |
| ② **Camera 不知道窗口，也不知道渲染后端** | `aspect` 由上层喂进来（`Camera::SetViewportSize`） | ✅ 已达成 |
| ③ **Renderer 不知道 Camera 之外的场景数据** | 它只接收「一份相机数据 + 一串渲染命令」 | ✅ 已达成 |
| ④ **后端之间互不认识** | `OpenGLRenderer.h` 永不 include `VulkanRenderer.h`，反之亦然 | ✅ 已达成 |

> **判断一个设计对不对，问一句**：*"如果明天换成 DirectX 后端，哪些文件要改？"*
> 正确答案是：**只改 `Render/OpenGL/` 那一层 + `RendererFactory.cpp` 的装配分支**。
> `main.cpp`、`Renderer.h`、`IMesh.h`、`IShader.h`、`Material.h` 一行都不用动。

### 3.3 依赖倒置的形态

```
        IShader  ◄──── OpenGLShader          IShader  ◄──── VulkanShader(未做)
        IMesh    ◄──── OpenGLMesh            IMesh    ◄──── VulkanMesh(未做)
        IRenderer◄──── OpenGLRenderer        IRenderer◄──── VulkanRenderer(占位)
             ▲                                     ▲
             │  main 只认识接口                     │
             │                                     │
   RendererFactory.cpp ── 认识所有实现（有条件地 include）
```

**所有实现指向接口，main 指向接口，只有工厂指向所有实现。**
这就是"依赖倒置"：不是没有依赖，而是把方向掰直了，并且把"知道具体类型"这件事**关进一个笼子**（工厂文件）。

---

## 四、后端选择机制（宏 + 工厂）

### 4.1 三个角色的分工

| 文件 | 职责 | 认识谁 |
|---|---|---|
| `Render/Renderer.h` | 声明抽象接口 + `CreateRenderer` 的**声明** | 只认识 `IMesh` / `IShader` / `Camera` |
| `Render/RendererFactory.h` | 工厂私有头：include `config.h`，再按宏 include 对应后端头 | 有条件地认识所有后端 |
| `Render/RendererFactory.cpp` | **唯一**决定"创建谁"的 `.cpp` | 有条件地认识所有后端 |
| `config.h` | 一行 `#define`，选择后端 | — |
| `main.cpp` | 调用 `CreateRenderer()`，用 `IRenderer*` | 只认识 `IRenderer` |

```cpp
// RendererFactory.cpp
#include "RendererFactory.h"

IRenderer* CreateRenderer(int width, int height) {
    #if defined(OPENGL_RENDERER)
        return new OpenGLRenderer(width, height);
    #elif defined(VULKAN_RENDERER)
        return new VulkanRenderer(width, height);
    #else
        #error "没有选择渲染后端：请定义 OPENGL_RENDERER 或 VULKAN_RENDERER"
    #endif
}
```

### 4.2 为什么工厂是"自由函数"，不是成员函数

```cpp
// Renderer.h —— 声明写在 class 外面（namespace 作用域）
class IRenderer { ... };
IRenderer* CreateRenderer(int width, int height);
```

**不能**写成 `IRenderer` 的虚成员函数，两个原因：

1. **名字可见性**：成员函数的名字不是裸名字，必须挂着对象用（`obj->CreateRenderer()`）。而 main 里第一时间还没有对象 → 裸调会报 `'CreateRenderer' was not declared in this scope`。
2. **逻辑上做不到**：虚函数调用要走虚表，**必须有对象实例才能查表**。而"创建第一个 renderer"本身还没有对象 —— 鸡生蛋。能打破这个循环的只能是**自由函数**或 **static 成员函数**。

> 对照：`CreateShader()` / `CreateMesh()` 写成成员函数是**对的** —— 调用时 renderer 已经存在，有"前置对象"。

### 4.3 ⚠️ 宏的作用范围 = 一次编译单元（踩过的坑）

**`#define` 只在一个 `.cpp` 的编译单元里有效，不跨文件。**

- 每个 `.cpp` 是一间**独立的房间**，`#define` 是贴在这间房间墙上的便条，隔壁房间看不见。
- 宏想跨文件生效，只有两条路：**① 写在一个被大家共同 include 的头里；② 由构建系统在命令行上加 `-D`**。

**踩坑记录**：曾把 `#define OPENGL_RENDERER` 写进 `main.cpp`，却在 `OpenGLRenderer.cpp` 里用 `#if defined(...)` 判断
→ 编 `OpenGLRenderer.cpp` 时宏不存在 → 整个分支被预处理器删掉 → `CreateRenderer` 返回 `nullptr` → `renderer->Init()` 读地址 0 → **`0xC0000005`，窗口都出不来**。

**现在的做法**：宏集中在 `config.h`（带 `#pragma once`），由 `RendererFactory.h` 显式 include。
配合工厂里的 `#error`，就算宏没配好也是**编译期**报错，而不是运行期崩溃。

**自检技巧**：想知道某个宏在某个编译单元里是否可见，可以在文件顶部临时加：
```cpp
#if !defined(OPENGL_RENDERER)
#error "OPENGL_RENDERER not visible in this translation unit"
#endif
```

### 4.4 加一个 Vulkan 后端要做什么

1. 实现 `Render/Vulkan/VulkanRenderer.cpp`（+ `VulkanShader` / `VulkanMesh`）。
2. 在 `CMakeLists.txt` 里加上新 `.cpp`，并把 `src/Render/Vulkan` 加进 `target_include_directories`。
3. 把 `config.h` 里的宏换成 `#define VULKAN_RENDERER`。
4. `RendererFactory.h` / `RendererFactory.cpp` 里的 `#elif defined(VULKAN_RENDERER)` 分支**已经写好了**。
5. `main.cpp` **一个字都不用动**。

> ⚠️ 后端清单在 `RendererFactory.h` 和 `RendererFactory.cpp` **两个文件里各有一份**，加后端时两处都要同步。
>
> ⚠️ `VulkanRenderer.h` 目前只是声明、没有 `.cpp`，所以切过去会**链接失败**（见 10.4 的 N12）。

---

## 五、Shadow Pass 是怎么接起来的

这一节是当前的重点，也是踩坑最多的地方。

### 5.1 概念：一张深度纹理 + 一个 FBO

| 概念 | 是什么 |
|---|---|
| **RT（渲染目标）** | 一个"可以往里画"的地方。默认 RT 就是窗口（默认 FBO = 0） |
| **FBO** | OpenGL 里实现 RT 的对象（一个容器） |
| **附件（attachment）** | FBO 上挂的缓冲：颜色 / 深度 / 模板 |
| **Renderbuffer vs Texture** | Renderbuffer **不能采样**；Texture 既能画、也能采样 |

**阴影必须用 Texture 当深度附件** —— 因为主 Pass 的片元着色器要把它当 `sampler2D` 采样。
（窗口自带的那张深度缓冲就是 Renderbuffer，所以不能直接拿来当阴影图。）

**为什么需要"渲染到纹理"**：主 Pass 的深度缓冲只有"相机能看见的表面"的信息；
而阴影要回答的是"从**灯**的角度看，这个点有没有被挡住" —— 这个信息主 Pass 里根本没有，必须多渲染一遍。

**ShadowPass = 把"相机"换成"灯"的一次渲染到纹理。**

### 5.2 当前的实际参数

| 项 | 值 | 位置 |
|---|---|---|
| 阴影图分辨率 | `1024 × 1024` | `OpenGLRenderer.cpp`（**硬编码了 3 处**，见 S5） |
| 深度纹理格式 | `GL_DEPTH_COMPONENT24` + `GL_FLOAT` | 同上 |
| 过滤 | `GL_NEAREST`（阴影图**不能线性过滤**） | 同上 |
| 环绕 | `GL_CLAMP_TO_BORDER`，border = `(1,1,1,1)` = "最远深度" | 同上 |
| 光源位置 | `lightPos = (2, 4, 0.8)`（**在场景斜上方**） | `OpenGLRenderer.h` |
| 灯光投影 | `glm::ortho(-10, 10, -10, 10, 1, 30)`（平行光用正交） | `OpenGLRenderer.h` + `InitShadowPass()` |
| 灯光视图 | `glm::lookAt(lightPos, origin, up)` | 同上 |
| 深度比较 bias | `0.002` | `basicfrag.glsl` 调用处 |
| PCF 采样 | 16 点泊松盘，`radius = 2.0` 纹素 | `basicfrag.glsl` |
| 半影实际宽度 | 1 纹素 ≈ 20/1024 ≈ **0.0195 世界单位** → radius 2 ≈ 0.04 单位（所以看起来**接近硬边**） | — |

### 5.3 每帧的三段顺序

```
1. PollEvents / camera.Update / RenderQueue::Sort

2. 【Shadow Pass】—— 只为记录深度
   glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
   glViewport(0, 0, 1024, 1024);          // ★ 视口要跟着变小
   glDepthMask(GL_TRUE);                  // ★ 必须！否则会被上一帧透明物体留下的 mask 挡住清屏
   mRenderState.depthWrite = true;        // ★ 同步状态缓存
   mRenderState.depthTest  = true; glEnable(GL_DEPTH_TEST);
   glClear(GL_DEPTH_BUFFER_BIT);          // 只清深度（没有颜色附件）

   mShadowShader->Use();
   for (cmd : queue) {
       if (cmd.material->renderState.blend != BlendMode::Opaque) continue;  // 跳过透明物体
       model = TransformToModelMatrix(cmd.transform);
       mShadowShader->SetMatrix(model, lightView, lightProjection);   // ★ 复用 SetMatrix
       cmd.mesh->Draw();
   }

   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);   // ★★ 忘了这句，主画面只画在左下角一小块

3. 【Base Pass】—— 正常画，并采样阴影图
   for (cmd : queue) {
       shader->Use();                                  // ① 先 Use（glUniform* 只对当前 program 生效）
       glActiveTexture(GL_TEXTURE0);                   // ② 绑到 0 号纹理单元
       glBindTexture(GL_TEXTURE_2D, depthTex);
       shader->SetInt ("shadowMap", 0);                // ③ 告诉采样器用 0 号单元
       shader->SetMat4("lightSpaceMatrix", lightProjection * lightView);
       ApplyRenderState(cmd.material->renderState);
       shader->SetMatrix(TransformToModelMatrix(cmd.transform), view, projection);
       shader->SetLight(lightPos, white);
       shader->SetCamera(cameraPos);
       cmd.mesh->Draw();
   }
```

**两个关键复用点**（说明抽象够用）：
- 深度着色器用的是 `ModelMatrix` / `ViewMatrix` / `ProjectionMatrix` 这套**同名 uniform**，所以 `IShader::SetMatrix` 直接就能用来传"灯光 V/P"，**一行都不用改**。
- 想在两个 Pass 用不同画法，**换一份 `Material` 就行** —— 这正是 `RenderCommand{mesh, material, transform}` 设计的红利。

### 5.4 深度比较的五步数学（`basicfrag.glsl`）

```glsl
vec4 lp = lightSpaceMatrix * vec4(worldPos, 1.0);
vec3 proj = lp.xyz / lp.w;              // ① 透视除法
proj = proj * 0.5 + 0.5;                // ② NDC[-1,1] → UV[0,1]（★ 忘了必错）
if (proj.z > 1.0) return 1.0;           // ③ 超出灯光远裁剪面 → 不在阴影
float closest = texture(shadowMap, proj.xy).r;   // ④ 查表
return (proj.z - bias) > closest ? 0.0 : 1.0;    // ⑤ 比较（0=在阴影，1=被照亮）
```

第 ② 步的 `*0.5 + 0.5` 是最经典的"忘了它"的地方：NDC 是 `[-1,1]`，而纹理坐标是 `[0,1]`。

### 5.5 PCF：把硬边磨软

**思路**：把"1 次深度比较"换成"在邻域里比较 N 次再取平均"。0/1 的硬边就变成了宽 `radius` 个纹素的渐变。

**为什么用泊松盘而不是 3×3 / 4×4 网格**：规则网格的采样点与 texel 网格同向 → 出现**结构化的条带/阶梯伪影**；
泊松盘"随机但保持最小间距" → 不聚集、不留空洞、不跟纹素网格对齐，同样采样数下噪点更"散"。

```glsl
#define PCF_SAMPLES 16
const vec2 kPoissonDisk[PCF_SAMPLES] = vec2[PCF_SAMPLES]( ... );  // 单位圆盘内的 16 个点

float ShadowFactorPCF(vec3 worldPos, float bias, float radiusTexel) {
    ... 前 3 步同上 ...
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));   // ★ 不要硬编码 1024
    float lit = 0.0;
    for (int i = 0; i < PCF_SAMPLES; ++i) {
        vec2 uv = proj.xy + kPoissonDisk[i] * radiusTexel * texelSize;   // ★ radius 单位是纹素
        lit += ShadowTap(uv, proj.z, bias);
    }
    return lit / float(PCF_SAMPLES);
}
```

**三个坑**：
1. `radius` 的单位是**纹素**，忘了乘 `texelSize` 半径就变成"半个屏幕"，阴影糊成一片。
2. 数组必须是 `const` 且大小是**编译期常量** —— 这是编译器能把循环**展开**的前提。
3. 采样点会跑到 `[0,1]` 之外，靠 `GL_CLAMP_TO_BORDER`（border=1.0=被照亮）兜底，所以**视锥边缘不会出现假阴影**。

### 5.6 调试手段：可视化 shadow map

出问题时，最快的定位方式是把深度纹理当灰度图打出来，而不是盯着最终的阴影看：

```glsl
// 临时插在 main() 里
vec4 lp = lightSpaceMatrix * vec4(posWS, 1.0);
vec2 uv = (lp.xy / lp.w) * 0.5 + 0.5;
fragColor = vec4(vec3(texture(shadowMap, uv).r), 1.0);
```

- 看到**猴头的白色剪影** → Shadow Pass 完全正确，问题在比较逻辑
- 全白 / 全黑 / 乱码 → 问题在 Shadow Pass（灯光矩阵、视口、FBO）

**再进一步**：把不同量分通道打出来，一次就能分辨是谁的问题：
```glsl
fragColor = vec4(lambert, inShadow, closest, 1.0);   // R=光照项, G=阴影判定, B=shadowMap 原始深度
```

### 5.7 踩过的三个坑（值得记住）

**坑 1：`lightPos` 写成了负数 → 灯跑到地底下**

`const glm::vec3 lightPos = glm::vec3(-2.0f, -4.0f, -0.8f);` 意味着**灯在 y = -4，地面之下**。
症状：单独看猴头"感觉阴影是对的"，但**一加新物体就全崩** —— 因为新加的 `plane2` 在 `y = -1`，正好夹在"灯（y=-4）"和"猴头（y≈0）"之间，**把整个猴头挡住了** → 猴头 100% 判定为"在阴影里" → 全白。

**坑 2：`lightView` 有两份互相矛盾的初始化**

头文件里的默认初始化用的是 `(2, 4, 0.8)`（灯在上方），而 `InitShadowPass()` 里又用 `lookAt(lightPos, ...)` **覆盖**了它。
后者生效 → **建 shadow map 用的是"下方的灯"，而写代码时脑子里想的是"上方的灯"**。

**坑 3：`lambert` 里多了一个负号，恰好"补偿"了坑 1**

`normalize(-mainLightPos)`：因为 `lightPos` 是负的，取负之后指向**上方** → 朝上的面被照亮 → **看起来像"灯在上方"**。
于是形成一组"看起来对、其实自相矛盾"的状态：

| | 实际的光 |
|---|---|
| **着色**（lambert） | 表现得像"灯在上方" |
| **阴影**（shadow map） | 用的是"灯在下方" |

**着色的光方向和投影的光方向相反** —— 这就是"单物体看着对、加个物体就崩"的根源。

**结论（最值得记住的一条）**：**着色用的光和投影用的光必须是同一个来源。**

---

## 六、文件清单

### 6.1 入口与配置

---

#### `src/main.cpp` ✅

**职责**：程序入口 + 渲染主循环。仍承担了较多职责（将来会被 `Application` / `Scene` 层接管）。

**关键点**：**不含任何 OpenGL 头文件**（`OpenGLRenderer.h` 的 include 被注释掉了）。只 include `<iostream>` / `Camera.h` / `Renderer.h` / `RenderQueue.h`。

**流程**：

```
1. IRenderer* renderer = CreateRenderer(WINDOW_WIDTH, WINDOW_HEIGHT);
   if (!renderer) { 报错退出 }              // 后端没编入时的兜底
   renderer->Init()                          // 建窗口 + 上下文 + GLAD + ShadowPass 资源

2. Camera camera; camera.SetViewportSize(renderer->GetWindowSize());

3. 三份 Material：
      material      (basicvertex/basicfrag)   depthTest = true           → 猴头 + plane2
      planeMaterial (groundNet*)              AlphaBlend, depthWrite=false → 半透明网格地面
   （plane2 直接复用了猴头的 material，所以它也采样阴影 → 能看到猴头投在它上面的影子）

4. 三份 Mesh：LoadObj → renderer->CreateMesh() → SetData(...)

5. 组装队列（★ 提交顺序故意是反的，用来验证排序生效）：
      Submit(plane ...)      // 半透明，先提交（应该被排到后面）
      Submit(mesh  ...)      // 不透明
      Submit(plane2...)      // 不透明，位置 (0,-1,0)、缩放 (3,1,3)

6. 主循环：
      PollEvents → camera.mouseX += 1.0（调试自转）→ camera.Update()
      → RenderQueue::Sort(cameraData.position)     // ★ 每帧排序
      → renderer->Clear()
      → renderer->ExecuteRenderCommands(queue.Commands(), cameraData)   // 内含 ShadowPass + BasePass
      → SwapBuffers
```

**依赖**：`Renderer.h`、`RenderQueue.h`、`Camera.h`、`Material.h`、`ObjLoader.h`、`<iostream>`
**被谁使用**：无（程序入口）

**已知问题**：
- 大量死代码：`vertices[]`、`indices[]`、局部 `view` / `projection` / `aspect` / `OrthoProjectionMatrix` / `viewWidth` / `viewHeight` 全部不再被使用
- `camera.mouseX += 1.0` 是调试代码，导致相机每帧持续自转
- `shader->BuildFromFiles(...)` 的**返回值被丢弃** → shader 加载失败时静默继续（N8）
- **`plane2` 没有 `delete`** → 泄漏（M1）
- **`plane2->SetData(objMeshData1)`** 用的是**第一个平面**的数据，不是 `objMeshData2`（M2，两个文件内容相同所以看不出问题）
- 两条 `LoadObj` 失败提前 `return -1` 的路径上，已 new 的 shader/mesh/renderer 没有释放（M3）
- 结尾的 `delete` 顺序是正确的：**先删资源、最后删 renderer**（因为 `~OpenGLRenderer` 会 `glfwTerminate`）

---

#### `src/config.h` ✅

**职责**：只做一件事 —— 选择渲染后端。

```cpp
#pragma once
// 用于选择渲染器后端的宏定义
#define OPENGL_RENDERER  // 定义宏，选择使用 OpenGL 渲染器
```

**说明**：由 `RendererFactory.h` 显式 include（谁用谁 include）。
它**不再是"万能头"** —— 以前它把 glad / glfw / Shader / Mesh / Camera / Renderer 全 include 进来，现在都拆干净了。

---

#### `src/Core/MatrixTools.h` 🚧

**职责**：目前放了两样东西 —— `Transform` 结构，以及三个矩阵小工具。

```cpp
struct Transform { glm::vec3 position; glm::vec3 rotation; glm::vec3 scale; };

inline glm::mat4 TransformToModelMatrix(const Transform& transform);              // T→R(x,y,z)→S
inline glm::mat4 GetLightSpaceViewMatrix(const glm::vec3& lightPos, ...);        // ★ 已成死代码
inline glm::mat4 GetLightSpaceProjectionMatrix(const glm::vec3& lightPos, ...);  // ★ 已成死代码
```

**关键点**：头文件里的**函数体**必须加 `inline`。
不加的话，每个 include 它的 `.cpp` 都会生成一份**强定义**（`nm` 里是 `T`），链接期直接报
`multiple definition of 'TransformToModelMatrix(Transform const&)'`。
加了 `inline` 变成**弱符号**（`nm` 里是 `W`），链接器自动合并。

**已知问题**：
- 两个 `GetLightSpace*Matrix` 已经没人调用了（`InitShadowPass()` 里直接内联了 `glm::ortho` / `glm::lookAt`）→ 死代码（S6）
- 它们的 `lightDir` 参数**完全没用过**
- `Transform` 是场景/渲染概念，却住在名字叫 `MatrixTools` 的文件里，命名不贴切（N10）

---

### 6.2 抽象接口层

---

#### `src/Render/IShader.h` ✅

```cpp
class IShader {
public:
    virtual ~IShader() = default;

    virtual bool BuildFromFiles(const std::string& vertexPath,
                                const std::string& fragmentPath) = 0;
    virtual unsigned int GetID() const = 0;              // ⚠️ 见 N5
    virtual void Use() = 0;
    virtual void SetMatrix(const glm::mat4& model,
                           const glm::mat4& view,
                           const glm::mat4& projection) = 0;
    virtual void SetLight(const glm::vec3& lightPos, const glm::vec3& lightColor) = 0;
    virtual void SetCamera(const glm::vec3& cameraPos) = 0;

    // ---- Shadow Pass 嵌入式：两个通用 uniform setter ----
    virtual void SetMat4(const std::string& name, const glm::mat4& value) = 0;
    virtual void SetInt (const std::string& name, int value) = 0;
};
```

**依赖**：`<string>`、`<glm/glm.hpp>`、`<glm/gtc/type_ptr.hpp>`（**不依赖任何图形 API 头**，干净的一层）

**为什么是这两个通用 setter，而不是 `SetTexture(ITexture*)`**：
纹理绑定（`glActiveTexture` / `glBindTexture`）可以在 `OpenGLRenderer` 里直接做（它本来就是 OpenGL 层）；
shader 只需要知道"采样器 uniform 指向第几个纹理单元"，那就是一个 `int`。
这样第一版**不需要新增 `ITexture` 抽象**，而且这两个方法对 Vulkan 后端也成立。

> ⚠️ **千万别加 `SetTextureID(unsigned int)` / `GetDepthTextureID()` 这类接口** —— 那等于把 `GLuint` 塞进抽象层。

**已知问题**：
- `GetID()` 返回"着色器程序 ID" —— 这是 **OpenGL 特有的概念**（Vulkan/D3D 没有"program id"），属于抽象层泄漏，而且全项目没人调用（N5）

---

#### `src/Render/IMesh.h` ✅

```cpp
class IMesh {
public:
    virtual ~IMesh() = default;
    virtual void SetData(const float* vertices, int vertexCount,
                         const unsigned int* indices, int indexCount) = 0;   // 裸数组
    virtual void SetData(const ObjMeshData& objMeshData) = 0;                 // OBJ 结果
    virtual void Draw() const = 0;
};
```

**依赖**：`ObjLoader.h`（→ `<string>` / `<vector>`）
**被谁实现**：`OpenGLMesh` —— **被谁使用**：`RenderCommand`、`OpenGLRenderer`、`main.cpp`

**已知问题**：`vertexCount` 的语义其实是"**float 个数**"（`OpenGLMesh` 里用它 × `sizeof(float)` 传给 `glBufferData`），名字有歧义（M4）

---

### 6.3 渲染核心层

---

#### `src/Render/Renderer.h` ✅

**职责**：`IRenderer` 抽象接口 + `BuiltInRendererFeatures` 枚举 + `CreateRenderer` 的**声明**。

```cpp
class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual glm::vec2 GetWindowSize() = 0;

    virtual bool  Init() = 0;                       // 建窗口 + 上下文 + GLAD + ShadowPass 资源
    virtual void* GetWindow() = 0;
    virtual void  SetClearColor(const glm::vec4& = glm::vec4(0.2f,0.3f,0.3f,1.0f)) = 0;
    virtual void  Clear() = 0;

    virtual bool WindowShouldClose() = 0;
    virtual void WindowTerminate() = 0;
    virtual void PollEvents() = 0;
    virtual void SwapBuffers() = 0;
    virtual void EnableRendererFeature(BuiltInRendererFeatures) = 0;
    virtual void DisableRendererFeature(BuiltInRendererFeatures) = 0;

    virtual IShader* CreateShader() = 0;            // 抽象工厂
    virtual IMesh*   CreateMesh()   = 0;

    virtual void ExecuteRenderCommands(const std::vector<RenderCommand>& cmds,
                                       const CameraData& cameraData) = 0;   // 内含 ShadowPass
};

IRenderer* CreateRenderer(int width, int height);   // 自由函数，见 4.2
```

**依赖**：`RenderCommand.h`、`IMesh.h`、`IShader.h`、`Camera.h`
**已知问题**：
- `EnableRendererFeature` / `DisableRendererFeature` **会绕过状态缓存**（R2）
- 接口偏胖（窗口 + 资源工厂 + 渲染命令混在一起），早晚要拆（N12）
- `GetWindow()` 返回 `void*`，类型不安全（N13）

---

#### `src/Render/RendererFactory.h` / `RendererFactory.cpp` ✅

**全项目唯一知道所有后端的装配点**（代码见 4.1）。
`RendererFactory.h` 只被 `RendererFactory.cpp` include，本质上是工厂的私有实现头。

---

#### `src/Render/RenderCommand.h` ✅

```cpp
struct RenderCommand {
    IMesh*    mesh;         // 画什么几何
    Material* material;     // 用什么材质（= IShader + 渲染状态）
    Transform transform;    // 放在哪里
};
```

**设计要点**：
- 只存 `Transform`，**不存算好的 `mat4`** —— 避免同一个 model 变换出现两个真相源
- 渲染时按 **T → R → S** 的顺序后乘
- **`mesh` / `material` 都是不拥有所有权的裸指针**
- 想在两个 Pass 用不同画法 → **换一份 Material 就行**，绘制代码是通用的

---

#### `src/Render/RenderQueue.h` / `RenderQueue.cpp` ✅

**职责**：收集渲染命令，并在**每帧**排序。

```cpp
class RenderQueue {
public:
    void Submit(const RenderCommand& cmd);
    void Clear();
    void Sort(const glm::vec3& cameraPos);        // 每帧调用（相机在动）
    const std::vector<RenderCommand>& Commands() const;
};
```

**排序 key 是 4 级元组**（`std::tuple` 的比较是**字典序**：前一项相等才看后一项）：

| 位置 | 字段 | 升序意味着 | 目的 |
|---|---|---|---|
| ① | `bool transparent` (`blend != Opaque`) | 不透明在前，透明在后 | **正确性**：混合必须最后做 |
| ② | `reinterpret_cast<uintptr_t>(shader)` | 同 shader 的物体相邻 | 性能：少切 `glUseProgram` |
| ③ | `RenderState`（用它自己的 `operator<`） | 同状态的物体相邻 | 性能：少切 `glEnable/glDepthMask/glBlendFunc` |
| ④ | `transparent ? -d2 : d2` | 不透明**近→远**；透明**远→近** | 正确性（透明）+ 性能（early-z） |

第 ④ 维那个 `-d2` 是个小技巧：**取负号把"要降序"翻译成"还是升序"**，这样一个升序比较就能同时表达两种相反的顺序。

**为什么 `reinterpret_cast<uintptr_t>`**：C++ 里"两个指针比大小"在标准上是未定义/实现定义的，转成整数后就是**纯整数比较，定义明确**。
代价是结果依赖地址（ASLR），每次运行顺序可能不同 —— 但**正确性不受影响**（同 shader 指针相等，一定相邻）。

**已知问题**：
- `key` 会被调用 `O(n log n)` 次，每次都重算一遍（含一次 `dot` + 一次 `RenderState` 拷贝）。物体上百时应改成"先算好 key 存起来再排"（N9）
- 手写的 `RenderQueue()` / `~RenderQueue()` 都是多余的，而且**手写析构会抑制隐式的移动构造**（N9）
- `~RenderQueue()` 里的 `mCommands.clear()` 什么也不多做（vector 析构本来就会清）

---

#### `src/Render/Material.h` / `Material.cpp` ✅

**职责**：把「用哪个 Shader」和「用什么渲染状态」绑在一起 —— 这就是"材质"的本质。

```cpp
enum class DepthFunc { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class CullMode  { Off, Front, Back };
enum class BlendMode { Opaque, AlphaBlend, Additive, Multiply };

struct RenderState {
    bool      depthTest  = false;             // ★ 默认值 = GL 的真实初始状态
    bool      depthWrite = true;
    DepthFunc depthFunc  = DepthFunc::Less;
    CullMode  cullMode   = CullMode::Off;     // ★ 同上
    BlendMode blend      = BlendMode::Opaque;

    bool operator<(const RenderState& o) const;   // 排序第 ③ 级用它
};

class Material {
public:
    Material(IShader* shader);
    void     SetShader(IShader*);
    IShader* GetShader() const;
    RenderState renderState;         // 公开数据，上层直接赋值即可配置

private:
    IShader* m_Shader = nullptr;     // ★ 不拥有所有权，只引用
};
```

**依赖**：`IShader.h`（**不间接依赖 glad** ✅）

**⚠️ 关于 `RenderState` 的默认值（重要）**

默认值被**刻意**设成了 **OpenGL 的真实初始状态**：`depthTest = false`、`cullMode = Off`、`blend = Opaque`。
这样 `OpenGLRenderer` 里的状态缓存（`mRenderState`）从一开始就"说的是真话"。

**代价**：`Material` 的默认状态是**"不测深度、不剔面"**。所以**新建材质时一定要显式写全状态**：
```cpp
material.renderState.depthTest = true;    // 不写的话，默认是不测深度的！
```

**已知问题**：构造函数建议加 `explicit`（N11）；缺 `castShadow` / `receiveShadow` 这类"用途"标志（S4/S10）

---

#### `src/Render/Camera/Camera.h` / `Camera.cpp` ✅

**职责**：轨道摄像机。不直接存位置，而是用**球坐标**三个数描述它在哪里，每帧换算成 View / Projection。

```cpp
struct CameraData { glm::vec3 position; glm::mat4 viewMatrix; glm::mat4 projectionMatrix; };

class Camera {
public:
    void  SetViewportSize(const glm::vec2& size);   // 同时更新 aspectRatio（带除零保护）
    float aspectRatio = 800.0f / 600.0f;

    float camYaw = 45.0f, camPitch = 20.0f, camRadius = 8.0f, sensitivity = 0.25f;
    bool   dragging = false;
    double mouseX = 0.0, mouseY = 0.0;

    void BeginDrag(double x, double y);
    void EndDrag();
    void Update();                                   // 球坐标 → position / view / projection
    CameraData GetCameraData();
};
```

**关键点**：始终看向原点；**不依赖窗口、不依赖渲染后端**（`aspect` 由上层喂进来）；构造时先 `Update()` 一次保证第一帧有效；`SetViewportSize` 对 `size.y == 0` 做了防除零。

**已知问题**：
- `Update()` 里 **`if (dragging)` 被注释掉了** → 每帧无条件套用鼠标位移（C1）
- 目前**没有任何地方注册鼠标回调**，所以 `mouseX` 正常情况下永远不会被写入（C3）
- `aspectRatio` 是 public 而 `viewportSize` 是 private，风格不一致

---

### 6.4 OpenGL 实现层

---

#### `src/Render/OpenGL/OpenGLRenderer.h` / `.cpp` ✅

**职责**：`IRenderer` 的 OpenGL 实现，**唯一允许出现 `gl*` / `glfw*` 的地方**。当前还额外承担了 Shadow Pass。

**公开成员**：

| 成员 | 说明 |
|---|---|
| `GLFWwindow* window` | public 的窗口句柄（建议改 private） |
| `Init()` | `CreateWindow()` **成功之后**才调 `InitShadowPass()` ✅ |
| `InitShadowPass()` | ⚠️ 目前是 public，应该 private（S11） |
| `GetWindowSize()` | 返回**构造时的固定值**（C5） |
| `Clear()` | 先 `glDepthMask(GL_TRUE)` + 同步缓存，再 `glClear(颜色 \| 深度)` ✅ |
| `CreateShader()` / `CreateMesh()` | 抽象工厂 |
| `ExecuteRenderCommands(...)` | **ShadowPass + BasePass**，见第五节 |

**私有的状态系统**：

| 成员 | 说明 |
|---|---|
| `mRenderState` | **状态缓存**：记录"上一次真正发给 GL 的状态" |
| `ApplyRenderState(state)` | 把材质想要的状态翻译成 GL 调用（**只在变化时才发**） |
| `RenderStateToOpenGL(...)` | 枚举 → GLenum（3 个重载） |

**私有的阴影相关**：

| 成员 | 说明 |
|---|---|
| `const glm::vec3 lightPos = (2, 4, 0.8)` | 光源位置（★ 之前误写成负数导致一片黑） |
| `lightProjection` / `lightView` | 灯光空间的两个矩阵（★ 与 `InitShadowPass()` 里重复计算，S6） |
| `mShadowShader` | 深度专用着色器（`OpenGLShader*`） |
| `depthTex` / `shadowFBO` | 阴影图纹理 + FBO |

**已知问题**：见 10.1（S1~S12）和 10.2（R1/R2/R5）

---

#### `src/Render/OpenGL/OpenGLShader.h` / `.cpp` ✅

**职责**：`IShader` 的 OpenGL 实现。

| 成员 | 说明 |
|---|---|
| `OpenGLShader()` | 默认构造，此时 `m_ID == 0` |
| `~OpenGLShader()` | `glDeleteProgram(m_ID)` ⚠️ **要求 GL 上下文仍存活** |
| `BuildFromFiles(vs, fs)` | 读文件 → 编译 → 链接 → 检查日志 |
| `Use()` | `glUseProgram(m_ID)` |
| `SetMatrix/SetLight/SetCamera` | 三个语义化 setter |
| `SetMat4(name, value)` / `SetInt(name, value)` | 两个通用 setter（Shadow Pass 用） |

**已知问题**：
1. `BuildFromFiles` **没写 `override`**（N6）—— 靠签名一致隐式覆盖，签名写错时不会报错
2. `const mVertexPath` / `mFragmentPath` 是早期"构造时构建"留下的**死成员**（N7）
3. **每次 `Set*` 都调用 `glGetUniformLocation`**（字符串查找 + 驱动调用）。location 从链接成功那刻起就不会变，应该查一次缓存（R5）
4. `glUniform*` 只对**当前绑定的 program** 生效 → 所有 `Set*` **必须在 `Use()` 之后**调用
5. 构造失败无法上报：错误只能通过 `BuildFromFiles` 的返回值传递，而 main 丢弃了它（N8）

---

#### `src/Render/OpenGL/OpenGLMesh.h` / `OpenGLMesh.cpp` ✅

**职责**：`IMesh` 的 OpenGL 实现（VAO + VBO + EBO）。

| 成员 | 说明 |
|---|---|
| `SetData(const float*, int, const unsigned int*, int)` | 直接给裸数组 |
| `SetData(const ObjMeshData&)` | 喂 OBJ 加载结果 |
| `Draw() const` | `glBindVertexArray` + `glDrawElements` |

**顶点属性布局**（着色器必须对得上）：

| location | 属性 | 分量 | 偏移 | 字节 |
|---|---|---|---|---|
| 0 | 位置（aPos） | 3 × float | 0 | 12 |
| 1 | 法线（aNormal） | 3 × float | 12 | 12 |
| 2 | UV（aTexCoor） | 2 × float | 24 | 8 |
| — | **步长（stride）** | — | — | **32** |

**已知问题**：
1. **同一个对象调两次 `SetData` 会泄漏**：`setDataInternal` 每次都 `glGen*` 并覆盖句柄，旧对象不删除（M4）
2. 文件里的注释还留着早期"每个顶点 6 个 float / stride 24"的说法，实际已经是 **8 个 float / stride 32**

---

#### `src/Render/Vulkan/VulkanRenderer.h` ⬜（占位）

复刻了 `IRenderer` 的接口清单，**只有 `.h`、没有 `.cpp`、没有任何实现**。
现在 `VULKAN_RENDERER` 没定义所以不参与编译；一旦切过去会**链接失败**（一堆 `undefined reference`）。建议标注 `// TODO: 未实现` 或先从 CMake 清单里拿掉（N12）。

---

### 6.5 工具与资源

---

#### `src/ObjLoader.h` / `ObjLoader.cpp` ✅

**职责**：读 `.obj`，并做**顶点展开** —— 把 OBJ 里**三套独立索引**（位置 v / UV vt / 法线 vn）转成 GPU 需要的**单一索引**格式。

```cpp
struct ObjMeshData {
    std::vector<float>        vertices;   // 交错：每顶点 8 个 float（位置 xyz | 法线 nxyz | UV uv）
    std::vector<unsigned int> indices;    // 每 3 个 = 一个三角形
    int VertexCount() const;              // = vertices.size() / 8
    int IndexCount()  const;
};
bool LoadObj(const std::string& path, ObjMeshData& out);
void PrintObjData(const ObjMeshData& data);
```

**依赖**：`tinyobjloader`（`ObjLoader.cpp` 里 `#define TINYOBJLOADER_IMPLEMENTATION` 后 include，实现只编译一次）
**被谁使用**：`main.cpp`、`IMesh.h`（类型）

> 顶点没有法线或 UV 时对应分量填 0；加载时会往控制台打印统计信息。

---

#### `src/Resources/ResourceManager.h` ⬜（空文件）

**规划职责**：加载/缓存 Shader、Mesh、Texture。

---

### 6.6 文档

| 文件 | 说明 |
|---|---|
| `src/目标渲染架构.md` | 架构规划。**⚠️ 已严重过时**：还在写 `Mesh*` / `Shader*`、`RenderQueue.h ⬜`、config.h 万能头、"下一步 1~5"（这五条现在都做完了） |
| `src/Core/想法.md` | 随手记的疑问（"是不是该有个 Manager？""矩阵该放哪里？"） |

---

## 七、文件之间的关系

### 7.1 Include（编译期依赖）

```
main.cpp
  ├─→ <iostream>
  ├─→ Camera.h ────────────→ glm（唯一一个完全干净的头）
  ├─→ RenderQueue.h ──→ RenderCommand.h ──→ MatrixTools.h（Transform）
  │                        │              ──→ IMesh.h ──→ ObjLoader.h
  │                        └──────────────→ Material.h ──→ IShader.h ──→ glm
  └─→ Renderer.h ──→ RenderCommand.h / IMesh.h / IShader.h / Camera.h

RendererFactory.cpp
  └─→ RendererFactory.h
        ├─→ config.h                       ← 宏在这里定义
        ├─→ Renderer.h
        └─→ OpenGL/OpenGLRenderer.h        ← 只在该宏定义时才 include
              └─→ <glad/glad.h> / <GLFW/glfw3.h> / OpenGLShader.h / OpenGLMesh.h

OpenGLRenderer.cpp ──→ OpenGLRenderer.h（+ OpenGLShader.h / OpenGLMesh.h）
OpenGLShader.cpp   ──→ OpenGLShader.h ──→ IShader.h
OpenGLMesh.cpp     ──→ OpenGLMesh.h   ──→ IMesh.h ──→ ObjLoader.h
Material.cpp       ──→ Material.h ──→ IShader.h
Camera.cpp         ──→ Camera.h
ObjLoader.cpp      ──→ ObjLoader.h + tinyobjloader
```

**读法**：只有 `Render/OpenGL/` 底下的文件连到 `<glad/glad.h>` / `<GLFW/glfw3.h>`。
**`main.cpp` 已经完全不连到 glad/GLFW 了** —— 这是解耦最直观的成果。

### 7.2 对象拥有关系（运行期）

```
main()
 ├── IRenderer* renderer ──(工厂 new / main delete)──> OpenGLRenderer
 │        │                                              ├── 拥有 → GLFWwindow
 │        │                                              ├── mRenderState（状态缓存）
 │        │                                              ├── mShadowShader（拥有 GL program）
 │        │                                              └── shadowFBO / depthTex ⚠️ 无释放
 │        │
 │        ├── CreateShader() ──> IShader*（OpenGLShader，拥有 GL program）
 │        └── CreateMesh()   ──> IMesh*  （OpenGLMesh，拥有 VAO/VBO/EBO）
 │                    ▲
 │                    │ 引用（不拥有）
 │              Material*（栈对象 material / planeMaterial）
 │                    ▲
 │              RenderCommand{ IMesh*, Material*, Transform }
 │
 ├── Camera camera
 └── RenderQueue renderQueueManager（栈对象，内含 vector<RenderCommand>）
```

> **所有权规则**
> 1. **谁创建谁销毁** —— 资源由 `renderer->CreateShader/CreateMesh()` 创建，由 `main` `delete`（接口都有虚析构，`delete IShader*` 安全）。
> 2. `Material` / `RenderCommand` **只引用不拥有**，生命周期必须短于被引用对象。
> 3. **⚠️ 销毁顺序**：`~OpenGLShader` 会 `glDeleteProgram`，`~OpenGLMesh` 会 `glDelete*` —— 都**要求 GL 上下文仍存活**。所以必须**先删资源、最后删 renderer**（因为 `~OpenGLRenderer` 里 `glfwTerminate`）。
>    `shadowFBO` / `depthTex` / `mShadowShader` 目前**没有释放**，要加的话必须放在 `glfwTerminate()` **之前**（S1）。

### 7.3 一帧的数据流

```
   main 主循环
       │
       ├─① renderer->PollEvents()             → GLFW 处理输入
       ├─② camera.Update()                    → 球坐标 → View / Projection
       ├─③ RenderQueue::Sort(cameraPos)       → 不透明在前；透明按距离远→近
       ├─④ renderer->Clear()                  → 先 glDepthMask(TRUE) 同步缓存，再清颜色+深度
       │
       └─⑤ renderer->ExecuteRenderCommands(queue.Commands(), cameraData)
              │
              ├─【Shadow Pass】切到 shadowFBO
              │     glViewport(1024²) → glDepthMask(TRUE) + 同步缓存 → glEnable(DEPTH_TEST)
              │     → glClear(DEPTH) → 用 mShadowShader 画所有"不透明"物体
              │                          SetMatrix(model, lightView, lightProjection)
              │     → 切回默认 FBO + 恢复视口
              │
              └─【Base Pass】逐条命令
                    ├─ shader->Use()
                    ├─ 绑定 depthTex 到 0 号纹理单元 + SetInt("shadowMap", 0)
                    ├─ SetMat4("lightSpaceMatrix", lightProjection * lightView)
                    ├─ ApplyRenderState(material->renderState)   ← 与 mRenderState 比较，只发变化的部分
                    ├─ SetMatrix(model, view, projection) + SetLight + SetCamera
                    └─ mesh->Draw()
              │
              ▼
        renderer->SwapBuffers()
```

---

## 八、关键概念

### 8.1 渲染命令（RenderCommand）

一条命令 = **"画什么 + 用什么 + 放在哪"**。它的意义是**把"收集"和"执行"解耦**：
上层负责收集（遍历、剔除、排序），后端负责执行（逐条翻译成 GL 调用）。这样后端不需要知道"场景里有什么"。

### 8.2 材质（Material）= IShader + 渲染状态

| 数据 | 谁决定 | 变化频率 | 放哪 |
|---|---|---|---|
| mesh（几何） | 物体 | 几乎不变 | `RenderCommand` |
| transform | 物体 | 每帧可能变 | `RenderCommand` |
| **shader 程序** | **材质** | 不变 | `Material` |
| **depth / blend / cull 状态** | **材质** | 不变 | `Material` |
| view / projection | 相机 | 每帧一次 | `CameraData` |
| 光源 | 场景 | 基本不变 | 目前是 `OpenGLRenderer` 的成员（待抽成帧数据） |

关键在于：**渲染状态不是"某个物体的数据"，而是"接下来这批 draw call 用什么规则画"**。

### 8.3 渲染队列与排序

两个目标：**正确性**（半透明必须最后画）和**性能**（同 shader / 同状态连在一起画）。见 6.3 的四级 key。

**一个"活的回归测试"**：`main.cpp` 里三条命令的**提交顺序是故意打乱的**（半透明先提交），
所以只要排序坏了，画面立刻变错 —— 相当于一个不需要测试框架的回归测试。（建议在这几行旁边加注释说明是故意的。）

### 8.4 渲染状态缓存（最容易出 bug 的地方）

`glEnable` / `glDisable` 改的是**上下文的全局状态**，而且发出去就要花驱动的钱。
所以用 `mRenderState` 缓存"上一次真正设过的状态"，只在变化时才发：

```cpp
if (renderState.depthTest != mRenderState.depthTest) {
    if (renderState.depthTest) glEnable(GL_DEPTH_TEST);
    else                       glDisable(GL_DEPTH_TEST);
    mRenderState.depthTest = renderState.depthTest;   // ★ 必须同步缓存
}
```

> **铁律：`mRenderState` 是 GL 真实状态的"镜像"。一旦不一致，之后所有 `!=` 比较都会失真。**
>
> 所以只有两个地方能写它：① 初始化时按 GL 的真实初始状态填一次；② **每次真的发出 GL 调用之后**立刻同步。
>
> **Shadow Pass 是这一条的重灾区**：它会 `glBindFramebuffer` / `glViewport` / `glDepthMask` / `glEnable(GL_DEPTH_TEST)`，
> 而这些**都不在缓存的管理范围内**。所以现在的做法是"在 Shadow Pass 里手动同步 `mRenderState`"（见 5.3）。
> 更干净的做法是让 Shadow Pass 也走 `ApplyRenderState`（传一份明确的 `RenderState`）。

> **另一个经典陷阱（已修复 ✅）**：`glClear(GL_DEPTH_BUFFER_BIT)` **受 `glDepthMask` 控制**！
> 上一帧若把 `depthWrite` 设成 `false`（透明地面就会），本帧的 `Clear()` 就**清不掉深度缓冲**。
> 现在 `Clear()` 开头会先 `glDepthMask(GL_TRUE)` 并同步缓存。
> **Shadow Pass 里清深度也一样**，所以那里也补了这两行。

### 8.5 矩阵归属

| 矩阵 | 由谁算 | 存在哪 | 怎么到 GPU |
|---|---|---|---|
| **Model** | `Transform`（位置/旋转/缩放） | `RenderCommand::transform` | `TransformToModelMatrix()` 现算 |
| **View** | `Camera::Update()` | `CameraData::viewMatrix` | 每帧随 `CameraData` 传给后端 |
| **Projection** | 同上，`aspect` 由 `SetViewportSize` 喂入 | `CameraData::projectionMatrix` | 同上 |
| **lightProjection / lightView** | `InitShadowPass()` | `OpenGLRenderer` 的私有成员 | Shadow Pass 用 `SetMatrix`，Base Pass 用 `SetMat4` |

**关键原则**：相机**不主动去问窗口大小**（让逻辑层反向依赖平台层是错的）；窗口大小的真相源在渲染后端，由**上层**转发。
**着色用的光和投影用的光必须来自同一个来源**（5.7 的坑）。

### 8.6 渲染循环顺序

```
① PollEvents()          ← 先处理输入（回调写 camera 状态）
② camera.Update()       ← 再更新逻辑（只改数据，不做任何 GL 调用）
③ Sort()                ← 排序（相机在动，必须每帧）
④ Clear()               ← 清屏（注意 depth mask 的坑）
⑤ ExecuteRenderCommands ← 内含 ShadowPass → BasePass
⑥ SwapBuffers()         ← 呈现
```

### 8.7 透明物体

半透明物体要正确工作，需要"三件套"：
```cpp
renderState.depthTest  = true;                  // 测深度：让远处被前面的不透明物体挡住
renderState.depthWrite = false;                 // 不写深度：不要挡住后面画的东西
renderState.blend      = BlendMode::AlphaBlend; // 开混合
```
再加上**绘制顺序**（由 `RenderQueue` 保证）：透明物体必须排在所有不透明物体之后。

> **"测深度"和"写深度"是两个独立开关**。深度写入对应 `glDepthMask`，**不是** `glEnable(GL_DEPTH_TEST)`。

---

## 九、Shader 与顶点格式约定

### 9.1 顶点属性（由 `OpenGLMesh` 设定，着色器必须一致）

```glsl
layout (location = 0) in vec3 aPos;      // 位置，偏移 0 字节
layout (location = 1) in vec3 aNormal;   // 法线，偏移 12 字节
layout (location = 2) in vec2 aTexCoor;  // UV，  偏移 24 字节
// 步长（stride）= 32 字节
```

### 9.2 Uniform 约定

| uniform | 类型 | basic* | groundNet* | depth* | 由谁设置 |
|---|---|---|---|---|---|
| `ModelMatrix` / `ViewMatrix` / `ProjectionMatrix` | mat4 | ✔ | ✔ | ✔（Shadow Pass 传的是灯光 V/P） | `SetMatrix` |
| `mainLightPos` / `mainLightColor` | vec3 | ✔ | ✔ | ✖ | `SetLight` |
| `CameraPos` | vec3 | ✖ | ✔ | ✖ | `SetCamera` |
| `shadowMap` | sampler2D | ✔ | ✖ ⚠️ | ✖ | `SetInt("shadowMap", 0)` |
| `lightSpaceMatrix` | mat4 | ✔ | ✖ ⚠️ | ✖ | `SetMat4(...)` |

> 对没有声明该 uniform 的 shader 调用无害：`glGetUniformLocation` 返回 -1，而 `glUniform*` 在 location 为 -1 时**被规范要求忽略**。

### 9.3 四套着色器对

| 着色器 | 用途 | 说明 |
|---|---|---|
| `basicvertex` / `basicfrag` | 猴头 + plane2 | 顶点输出 `vertexNormal` + `posWS`；片元做 `max(0, dot(N, normalize(mainLightPos)))` + **PCF 阴影** |
| `groundNetVertex` / `groundNetFrag` | 半透明网格地面 | 用 `fwidth` 做屏幕空间抗锯齿的多层网格；按距离淡出；**★ 完全不采样阴影图** |
| `shadow_mapping_depth_vert` / `_frag` | Shadow Pass | 顶点只输出 `gl_Position`；**片元 `main(){}` 是空的**（只有深度有意义） |

> **地面（栅格）不接收阴影是设计选择** —— 它是纯可视化用的辅助栅格（带坐标轴），不参与光照/阴影。
> 能看见猴头影子的那块白色平板是 `plane2`，它复用了猴头的 `material`（所以会采样 shadow map）。

---

## 十、已知问题与 TODO

### 10.1 Shadow Pass

| # | 问题 | 后果 / 触发条件 | 建议 |
|---|---|---|---|
| **S1** | `~OpenGLRenderer` 只调 `glfwTerminate()`，`depthTex` / `shadowFBO` / `mShadowShader` 都没释放 | GPU 资源泄漏；`mShadowShader` 的 `glDeleteProgram` 永远不执行 | 在析构里、**`glfwTerminate()` 之前**释放 |
| **S2** | base pass 把 `depthTex` 绑到 0 号纹理单元后**从不解绑**；下一帧 Shadow Pass 又把同一张纹理当**深度附件**绑定 | 同一张纹理同时被"写"和"采样" → **未定义行为**（反馈循环），驱动行为可能诡异 | base pass 结束后 `glBindTexture(GL_TEXTURE_2D, 0)`；或用独立纹理单元、在 Shadow Pass 前解绑 |
| **S3** | `basicfrag` 里 `float shadow = ShadowFactor(posWS);` 算完**没被使用**（只用了 `shadowPCF`） | 死代码 + 未使用变量 | 删掉那一行（或删掉整个非 PCF 的 `ShadowFactor`） |
| **S4** | Shadow Pass 用 `blend != Opaque` 来筛"投影者" | "是否透明"和"是否投影"是两件事（不透明物体也应该能设置成不投影） | 给 `Material` 加 `bool castShadow` |
| **S5** | `1024`（阴影图尺寸）**硬编码在 2 处**：`glTexImage2D`、`glViewport` | 改分辨率要改两处，容易漏 | 抽 `constexpr int SHADOW_SIZE`，两处都引用它 |
| **S6** | 灯光空间矩阵算了两遍（头里默认初始化 + `InitShadowPass()` 覆盖）；`lightDir` 算了没用；`MatrixTools.h` 里的 `GetLightSpace*Matrix` 已成死代码 | 重复 + 死代码 | 只保留 `InitShadowPass()` 里一处；删掉死函数 |
| **S7** | `lightProjection * lightView` 在**每个物体**上重复计算 | 每帧多做 n 次矩阵乘法 | 提到循环外算一次 |
| **S8** | `glEnable(GL_MULTISAMPLE)` 在 `ExecuteRenderCommands` 里每帧调用 | 冗余 | 挪到 `CreateWindow()` 里 |
| **S9** | PCF 的 `bias = 0.002` / `radius = 2.0` 硬编码在调用处 | 调参要改 shader 源码并重编 | 提成 uniform，或至少放 `#define` |
| ~~S10~~ | ~~`groundNetFrag.glsl` 不采样阴影图~~ | **按设计如此，不是缺陷**：地面是纯可视化用的栅格（含坐标轴），不参与光照/阴影 | 不用改 |
| **S11** | `InitShadowPass()` 是 public | 它只是初始化的一步，不该对外暴露 | 移到 `private` |
| **S12** | `VulkanRenderer.h` 只有声明、没有 `.cpp` | 切到 `VULKAN_RENDERER` 会链接失败 | 标 `// TODO: 未实现` 或先从 CMake 清单移除 |
| **S13** | base pass 恢复视口用的是 `WINDOW_WIDTH/HEIGHT`（**逻辑窗口尺寸**），而 `glViewport` 要的是 **framebuffer 像素** | 在显示缩放 ≠ 100% 的机器上（HiDPI），上下文创建后的默认视口是 framebuffer 尺寸，但第一帧结束后会被改成逻辑尺寸 → **主画面缩到左下角一块**。本机 100% 缩放所以看不出来 | 改成 `glfwGetFramebufferSize` 的真实像素尺寸；配合 C5 一起修 |

### 10.2 渲染状态与绘制

| # | 问题 | 后果 | 建议 |
|---|---|---|---|
| **R1** | `RenderStateToOpenGL(BlendMode)` 返回单个值，却用来喂 `glBlendFunc` 的两个参数 | `AlphaBlend` 恰好正确；**`Additive` / `Multiply` 是错的** | 改成 `switch`，一个 case 里同时设 `glBlendFunc(src, dst)` |
| **R2** | `EnableRendererFeature` / `DisableRendererFeature` 直接 `glEnable`/`glDisable` | **绕过状态缓存** → 缓存失真 → 之后所有状态判断都错 | 删掉 main 里的调用；或让它们同步更新 `mRenderState` |
| **R5** | 每次 `Set*` 都 `glGetUniformLocation` | 每帧几百次无谓的字符串查找 | 链接成功后查一次并缓存 location |
| **R6** | `basicfrag` 里 `normalize(mainLightPos)` 把**灯的位置**当**方向**用（`SetLight` 传进去的是 `lightPos`） | 物体离原点越远，"指向光源的方向"错得越厉害 —— 现在两个物体都在原点附近所以看不出来 | `SetLight` 改传真正的**光线方向**（平行光＝固定方向；点光＝`normalize(lightPos - worldPos)`），或把 uniform 改名成 `lightDir` |

> ✅ **已修复**：`Clear()` 的深度 mask 问题；渲染队列没有排序（现在有 `RenderQueue::Sort`）。

### 10.3 摄像机与输入

| # | 问题 | 建议 |
|---|---|---|
| **C1** | `Camera.cpp` 里 `if (dragging)` 被注释掉 | 恢复它，否则"不按左键光移动鼠标"也会旋转 |
| **C2** | `main.cpp` 里 `camera.mouseX += 1.0` | 删掉（让相机自转的调试代码） |
| **C3** | 没有任何鼠标回调注册 | `glfwSetCursorPosCallback` / `glfwSetMouseButtonCallback` → 转发给 `Camera::BeginDrag/EndDrag`。**后端只暴露"回调注册"接口，由上层转发**，不要让 `OpenGLRenderer` 认识 `Camera` |
| **C4** | 没有处理窗口 resize | `glfwSetFramebufferSizeCallback` + 回调里 `glViewport` + 更新相机 aspect |
| **C5** | `GetWindowSize()` 返回构造时的固定值 | 改成 `glfwGetFramebufferSize(window, ...)`，否则拉伸窗口画面会变形（且 S13 也一起解决） |
| **C6** | `aspectRatio` public / `viewportSize` private，两者可能不同步 | 统一成 private + 单一入口 |

### 10.4 架构与解耦

| # | 问题 | 建议 |
|---|---|---|
| **N5** | `IShader::GetID()` 把"program id"这个 OpenGL 概念放进抽象接口，且没人用 | 删掉，或改成不暴露底层句柄的语义 |
| **N6** | `OpenGLShader::BuildFromFiles` 没写 `override` | 加上（签名写错时能编译期发现） |
| **N7** | `OpenGLShader` 的 `const mVertexPath / mFragmentPath` 已无人使用 | 删掉 |
| **N8** | `main` 丢弃 `BuildFromFiles` 的返回值 | shader 加载失败时静默继续，之后画面全黑很难查 → 检查返回值 |
| **N9** | `RenderQueue`：`key` 重复算 `O(n log n)` 次；手写构造/析构多余且**抑制移动语义** | 预计算 key；删掉手写的构造/析构（零规则） |
| **N10** | `MatrixTools.h` 里放着 `Transform`；两个 `GetLightSpace*` 已成死代码 | 拆成 `Core/Transform.h` + `Core/MathTools.h`；删死代码 |
| **N11** | `Material` 构造缺 `explicit` | 加上 |
| **N12** | `IRenderer` 接口偏胖（窗口 + 资源工厂 + 渲染命令混在一起） | 早晚拆成 `IDevice`（资源工厂）/ `IRenderer`（帧） |
| **N13** | `IRenderer::GetWindow()` 返回 `void*` | 需要时引入前向声明的句柄类型 |
| **N14** | 后端清单在 `RendererFactory.h` 和 `.cpp` 里各一份 | 加后端要改两处；写注释互相提醒 |

> ✅ **已修复**：`Shader`/`Mesh` 直接调 `gl*` 且放在 `src` 根目录（已拆成 `IShader`/`IMesh` + `Render/OpenGL/*`）；
> `Material` 间接依赖 glad；`IRenderer::Render(Mesh*,Shader*,mat4&)` 空实现（已删）；
> `config.h` 万能头（已瘦身 + 加 `#pragma once`）；`RendererFactory` 的 `return nullptr`（已改 `#error`）；
> `RendererFactory.h` 对 `config.h` 的隐式依赖（已显式 include）；`MatrixTools.h` 缺 `inline`（已加）。

### 10.5 资源生命周期与内存

| # | 问题 | 后果 | 建议 |
|---|---|---|---|
| **M1** | `main` 结尾**没有 `delete plane2`** | 泄漏 | 补上 |
| **M2** | `plane2->SetData(objMeshData1)` 用了**第一个平面**的数据（应该是 `objMeshData2`） | 内容相同所以现在看不出问题，但 `objMeshData2` 白加载了 | 改成 `objMeshData2`；或干脆只加载一份 |
| **M3** | `main` 提前 `return -1` 的路径不释放已创建的资源 | 泄漏（进程即将退出，影响小） | 收进一个 `Application` 类 |
| **M4** | `OpenGLMesh::SetData` 重复调用会 `glGen*` 并覆盖旧句柄 | 旧的 VAO/VBO/EBO 泄漏 | 先删旧的，或加"只允许设置一次"的断言 |

### 10.6 清理项

- `main.cpp`：`vertices` / `indices` / 局部 `view` / `projection` / `aspect` / `OrthoProjectionMatrix` / `viewWidth` / `viewHeight` 全是死代码
- `main.cpp`：`//#include "OpenGLRenderer.h"` 和 `//IRenderer* renderer = new OpenGLRenderer(...)` 两条死注释
- `OpenGLMesh.h` / `OpenGLMesh.cpp`：注释里的"每个顶点 6 个 float / stride 24"已过时（实际 8 个 / 32）
- `OpenGLRenderer.h`：`window` 是 public，建议改 private
- `Resources/ResourceManager.h`：空文件
- `目标渲染架构.md`：内容已严重过时（`Mesh*` / `Shader*` / `RenderQueue ⬜` / config.h 万能头 / 已完成的"下一步"）
- `mesh/*.mtl`：文件里只有注释、没有材质定义，所以模型用默认材质

### 10.7 下一步（按优先级）

1. **把"光照方向"的语义理顺** —— `mainLightPos` 是灯的位置却被当方向用（R6）。现在物体都在原点附近所以看不出来，但这是"着色的光和投影的光不一致"那类 bug 的同一个根
2. **RT 的绑定/解绑收口** —— 引入 `IRenderTarget`（`Bind()` / `Unbind()` 内部管 FBO + 视口），一次解决 S2（反馈循环）、S5（硬编码）、S13（视口用错尺寸）、S1（资源释放）
3. **`Material::castShadow`** —— 替代 Shadow Pass 里用 `blend != Opaque` 筛投影者（S4）
4. **修摄像机输入通路** —— C1~C4（恢复 `if(dragging)`、删掉自转、注册回调、处理 resize）
5. **修 R1 / R2** —— blend 映射 + 越过状态缓存的接口
6. **小收口** —— `SHADOW_SIZE` 常量、灯光矩阵只算一次、删掉 `ShadowFactor` 死代码、`Set*` 的 location 缓存
7. **再往后** —— `FrameData`（把光源/清屏色也变成"每帧传一次"）、`Scene` 层、`ResourceManager`、`RenderTarget`（多 Pass / 后处理）

---

## 十一、变更记录

### 第一轮：接口解耦 + 后端选择

| 项目 | 之前 | 现在 |
|---|---|---|
| Shader / Mesh | `src/Shader.h/.cpp`、`src/Mesh.h/.cpp`（直接调 gl*，在 src 根目录） | `Render/IShader.h` + `Render/IMesh.h`（接口）+ `Render/OpenGL/OpenGLShader.*` + `OpenGLMesh.*`（实现） |
| Material | 持有 `Shader*` → 间接依赖 glad | 持有 `IShader*` ✅ |
| 资源创建 | `main` 里 `new Shader/Mesh` | `renderer->CreateShader()` / `CreateMesh()`（抽象工厂） |
| 后端创建 | `main` 里 `new OpenGLRenderer` | `CreateRenderer()` 自由函数 + `RendererFactory.cpp` 装配 |
| 后端选择 | 无 | 编译期宏 `OPENGL_RENDERER` / `VULKAN_RENDERER` + `#error` 兜底 |
| `config.h` | "万能头" | 只剩一行宏（+ `#pragma once`） |
| `main.cpp` 的 include | `OpenGLShader.h` / `OpenGLMesh.h` / `OpenGLRenderer.h` | 只剩 `Camera.h` / `Renderer.h` / `RenderQueue.h` / `<iostream>` |
| 遗留接口 | `IRenderer::Render(Mesh*, Shader*, mat4&)` 空实现 | 已删除 |
| `Clear()` 的深度 mask | 受上一帧 `depthWrite` 影响 | 先 `glDepthMask(TRUE)` 并同步缓存 |
| 删除的旧文件 | — | `Shader.*` / `Mesh.*` / `RenderObject.*` |

### 第二轮：渲染队列 + Shadow Pass

| 项目 | 说明 |
|---|---|
| **渲染队列** | 新增 `RenderQueue`（收集 + 4 级 key 排序）；`Sort()` 进主循环（相机在动，必须每帧） |
| **Shadow Pass** | 单张 1024² `DEPTH_COMPONENT24` 纹理 + FBO，从灯光空间渲染所有不透明物体 |
| **PCF** | 16 点泊松盘 + `textureSize` 推导纹素尺寸；`radius = 2.0` 纹素 |
| **通用 uniform setter** | `IShader::SetMat4(name, value)` / `SetInt(name, value)` |
| **深度专用着色器** | `shadow_mapping_depth_vert/frag.glsl`（复用同名 MVP uniform，所以 `SetMatrix` 直接可用） |
| **`MatrixTools.h`** | 抽出 `Transform` + `TransformToModelMatrix`（三个函数加 `inline` 解决 `multiple definition`） |
| **修掉的 bug** | `lightPos` 误写成负数（灯在地底下）→ 改成正的；`lightView` 双初始化；`lambert` 里多余的负号；`1.0 - ShadowFactor` 反号；`MatrixTools.h` 缺 `inline`；`RendererFactory` 的 `return nullptr` → `#error`；`RendererFactory.h` 补 `include config.h`；CMake 补 `src/Core` include 路径；`.gitignore` 放行 `mesh/*.obj` |

### 现在的架构一句话总结

> **接口在中间，实现在下层，装配只有一处，应用层只认识接口；两个 Pass 共用同一套 `RenderCommand` / `Material`，靠"换一份 Material"来换画法。**

**验收标准**：`main.cpp` 里搜不到 `OpenGL` / `glad` / `glfw`（除了两条被注释掉的遗留写法）。

### 还没做完的

见 [10.1](#101-shadow-pass) ~ [10.7](#107-下一步按优先级)。
眼下最值得做的三件：**R6（光照方向语义）**、**S2 + S13（RT 的绑定/视口收口，顺带解决 S1/S5）**、**C1~C4（摄像机输入通路）**。
