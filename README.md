# OpenGLRenderer

一个从零手写的 OpenGL 渲染引擎（学习项目），目标是把"散落在 main 里的 OpenGL 调用"逐步重构成一套分层的渲染架构。

**技术栈**：C++17 · OpenGL 4.6 Core Profile · GLFW · GLAD · GLM · tinyobjloader

**当前能跑出来的东西**：一个轨道摄像机视角下，猴头模型（不透明、灯在斜上方所以顶部最亮）**把影子投在下方的白色平板上**，外加一片半透明的无限网格地面。猴头的凹陷处有屏幕空间环境光遮蔽（SSAO）压暗，物体之间有屏幕空间间接光（SSGI）互相"漏"一点光，阴影边缘是屏幕空间 PCSS 算出的柔和半影。另外**材质已经能带颜色**（`Material::baseColor`），所以画面上有白/红/绿三个平面可用于验证 SSGI 的颜色渗透。

**当前架构状态**：
- 抽象接口 + 具体实现 + 工厂的第一轮解耦已完成 —— `main.cpp` 里**不出现任何 OpenGL 头文件**。
- 后端由编译期宏选择、由唯一的工厂文件装配；`OpenGLRenderer.h` 与 `VulkanRenderer.h` 互不认识。
- **渲染管线已 Pass 化**：`OpenGLRenderer` 不再自己持有任何"中间 RT"，改成一个按 `Stage()` 排序的 `vector<unique_ptr<OpenGLRenderPass>>`；Pass 之间只通过一个每帧的 `OpenGLRenderContext` 交换数据。
- 共 **8 个 Pass**：`ShadowPass(100)` → `GBufferPass(200)` → `SSAOPass(300)` → `SSAOBlurPass(310)` → `ScreenShadowPass(350)` → `ScreenShadowBlurPass(360)` → `SSGIPass(370)` → `SSGIBlurPass(371)` → `BasePass(500)`。
- **PCSS 已经从材质着色器里搬到了屏幕空间**：`basicfrag.glsl` 只剩 3 次纹理采样（阴影 / AO / 间接光），不再自己跑 32 次采样的 blocker search。
- **SSGI 可以运行时开关**（按 **G**）；关掉时纹理被清成 0，画面与"没有 SSGI"逐像素一致。
- **材质带颜色**：`Material::baseColor`（+ `IShader::SetVec3`），在 `BasicPass` 里作为 `baseColor` uniform 传给着色器。

> **一个反复出现的结构**：只要一个效果"**算一遍会带噪点**"，就需要"**算 → 再起一趟去噪**"（两张纹理、两个 Pass）
> —— 因为模糊要读邻居的值，而那个值正是这一趟正在写的纹理（同一张纹理既读又写 = 反馈循环）。
> AO、屏幕空间阴影、SSGI 现在都是这个结构（`XxxPass` + `XxxBlurPass`）。

---

## 目录

- [一、构建与运行](#一构建与运行)
- [二、目录结构](#二目录结构)
- [三、分层与依赖规则](#三分层与依赖规则)
- [四、后端选择机制（宏 + 工厂）](#四后端选择机制宏--工厂)
- [五、Pass 渲染管线（当前的核心）](#五pass-渲染管线当前的核心)
- [六、屏幕空间三件套：AO / PCSS / SSGI](#六屏幕空间三件套ao--pcss--ssgi)
- [七、文件清单](#七文件清单)
- [八、文件之间的关系](#八文件之间的关系)
- [九、关键概念](#九关键概念)
- [十、Shader 与顶点格式约定](#十shader-与顶点格式约定)
- [十一、已知问题与 TODO](#十一已知问题与-todo)
- [十二、变更记录](#十二变更记录)

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
    │   ├── MatrixTools.h             ✅ Transform + TransformToModelMatrix（只剩这一个函数）
    │   └── 想法.md                   ✅ 随手记的疑问（沟通用）
    │
    ├── Render/                       🚧 渲染核心层（与图形 API 无关）
    │   ├── Renderer.h                ✅ IRenderer 抽象接口 + CreateRenderer 声明
    │   ├── RendererFactory.h         ✅ 工厂私有头：按宏 include 对应后端
    │   ├── RendererFactory.cpp       ✅ 全项目唯一的"装配点"
    │   ├── RenderCommand.h           ✅ RenderCommand（mesh + material + transform）
    │   ├── RenderQueue.h / .cpp      ✅ 收集 + 排序（不透明在前、透明按距离远→近）
    │   ├── Material.h / .cpp         ✅ 材质：IShader + RenderState
    │   ├── IShader.h                 ✅ 着色器抽象接口（含 3 个通用 uniform setter）
    │   ├── IMesh.h                   ✅ 网格抽象接口
    │   ├── Camera/
    │   │   ├── Camera.h              ✅ CameraData + Camera（轨道相机）
    │   │   └── Camera.cpp
    │   ├── OpenGL/                   ✅ 具体 API 实现（唯一允许出现 gl* / glfw* 的地方）
    │   │   ├── OpenGLRenderer.h/.cpp  ✅ 窗口/上下文 + Pass 装配与调度（★ 已瘦身）
    │   │   ├── OpenGLShader.h/.cpp    ✅ IShader 实现
    │   │   ├── OpenGLMesh.h/.cpp      ✅ IMesh 实现（VAO+VBO+EBO）
    │   │   └── Passes/                ✅ ★ 各渲染 Pass（每个自己拥有 RT，RAII）
    │   │       ├── OpenGLRenderPass.h        Pass 基类 + OpenGLRenderContext + Stage 枚举
    │   │       ├── FullscreenQuad.h/.cpp     全屏三角形（后处理地基）
    │   │       ├── ShadowPass.h/.cpp         灯光空间深度图 + 灯光 V/P → ctx
    │   │       ├── GBufferPass.h/.cpp        世界法线 / 世界深度（MRT）
    │   │       ├── SSAOPass.h/.cpp           环境光遮蔽（32 核 + range/normal check）
    │   │       ├── SSAOBlurPass.h/.cpp       按深度加权的 5×5 模糊
    │   │       ├── ScreenShadowPass.h/.cpp   屏幕空间 PCSS（半分辨率）
    │   │       ├── ScreenShadowBlurPass.*    双边模糊去噪
    │   │       ├── SSGIPass.h/.cpp           屏幕空间全局光照（单次弹射，半分辨率，可开关）
    │   │       ├── SSGIBlurPass.h/.cpp       给 SSGI 去噪（深度 + 法线加权双边模糊）
    │   │       └── BasePass.h/.cpp           用材质把队列画一遍（唯一"有画面输出"的 Pass）
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
    ├── shaders/                      ✅ 共 16 个（见 10.3）
    │   ├── basicvertex / basicfrag             猴头 + 三个平面（片元只做 3 次屏幕空间采样）
    │   ├── groundNetVertex / groundNetFrag     半透明网格地面（fwidth 抗锯齿）
    │   ├── shadow_mapping_depth_vert / _frag   Shadow Pass 的深度专用着色器
    │   ├── gbuffer_vert / gbuffer_frag         G-Buffer（MRT：法线 + 世界深度）
    │   ├── fullscreen_vert                     全屏三角形（所有后处理共用）
    │   ├── ssao_frag / ssao_blur_frag          SSAO + 深度加权模糊
    │   ├── screen_shadow_frag / _blur_frag     屏幕空间 PCSS + 双边模糊
    │   └── ssgi_frag / ssgi_blur_frag          SSGI（单次弹射）+ 深度/法线加权去噪
    │
    └── 目标渲染架构.md                🚧 架构规划文档（★ 部分内容已过时）
```

**不属于本项目的目录**：`build/`（CMake 生成物，已在 `.gitignore` 里）、`.commandcode/`（工具配置）。

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
   │   OpenGLRenderer : IRenderer                       │
   │        └─ 只做三件事：建窗口/上下文、填 ctx、按 Stage 跑 Pass │
   │   OpenGLRenderer 的成员：vector<unique_ptr<Pass>>  │
   │                              │                      │
   │   ┌──────────────────────────┴───────────────────┐ │
   │   │ Passes/  每个 Pass 自己持有自己的 RT（RAII）  │ │
   │   │   OpenGLRenderPass  基类 + ctx + Stage 枚举   │ │
   │   │   Shadow → GBuffer → SSAO → SSAOBlur          │ │
   │   │   → ScreenShadow → ScreenShadowBlur → SSGI    │ │
   │   │   → SSGIBlur → BasePass（把队列画出去）        │ │
   │   └──────────────────────────────────────────────┘ │
   │   OpenGLShader : IShader      OpenGLMesh : IMesh   │
   └──────────────────────────────────────────────────┘

   Core/MatrixTools.h          ← Transform + 矩阵小工具（被 Render 层使用）
   Render/RendererFactory.cpp  ← 唯一的装配点：知道所有后端，负责 new 出具体实现
```

### 3.2 五条硬规则

| 规则 | 说明 | 现状 |
|---|---|---|
| ① **只有 `Render/OpenGL/` 里可以出现 `gl*` / `glfw*`** | 其它层一律不碰图形 API | ✅ 已达成 |
| ② **Camera 不知道窗口，也不知道渲染后端** | `aspect` 由上层喂进来（`Camera::SetViewportSize`） | ✅ 已达成 |
| ③ **Renderer 不知道 Camera 之外的场景数据** | 它只接收「一份相机数据 + 一串渲染命令」 | ✅ 已达成 |
| ④ **后端之间互不认识** | `OpenGLRenderer.h` 永不 include `VulkanRenderer.h`，反之亦然 | ✅ 已达成 |
| ⑤ **Pass 只认识 `OpenGLRenderContext`** | Pass 不认识 Renderer、不认识别的 Pass、不认识 Camera 类 | ✅ 新达成（见第五节） |

> **判断一个设计对不对，问一句**：*"如果明天换成 DirectX 后端，哪些文件要改？"*
> 正确答案是：**只改 `Render/OpenGL/` 那一层 + `RendererFactory.cpp` 的装配分支**。
> `main.cpp`、`Renderer.h`、`IMesh.h`、`IShader.h`、`Material.h` 一行都不用动。
>
> **Pass 化之后这条更好回答了**：加/删/重排一个后处理效果，**只需要动 `Passes/` 里的文件**，
> `OpenGLRenderer.cpp` 的 `ExecuteRenderCommands` 一个字都不用改（它只是"按排序跑一遍"）。

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
> ⚠️ `VulkanRenderer.h` 目前只是声明、没有 `.cpp`，所以切过去会**链接失败**（见 11.1 的 S12）。
> 更要紧的是：**Pass 管线（`Passes/OpenGLRenderPass.h` 和那 8 个 Pass）本身是 OpenGL 专属的**，
> `OpenGLRenderContext` 里直接存着 `unsigned int` 纹理句柄。换后端不是"补一个 `.cpp`"就完事，那套东西要重做一遍。

---

## 五、Pass 渲染管线（当前的核心）

这一节是当前的重点。

### 5.1 为什么要有 Pass 基类

在 Pass 化之前，所有"渲染一遍"的逻辑都堆在 `OpenGLRenderer::ExecuteRenderCommands` 里，
中间 RT（`shadowFBO` / `depthTex` / `mShadowShader` / GBuffer 的几张纹理…）也全是 `OpenGLRenderer` 的成员。
后果是：**每加一个效果，Render 类就要多一堆成员 + 多一段硬编码调用**，而且资源释放要自己在析构里一个个写。

现在改成"**一个 Pass 一个类**"，三条原则：

| 原则 | 说明 |
|---|---|
| ① **Pass 不认识 Renderer** | 输入全从 `Execute(ctx)` 进来，输出写回 `ctx` |
| ② **Pass 自己的 GPU 资源自己持有** | `Setup()` 建、析构删（RAII）；**不允许**再把中间 RT 塞进 Renderer |
| ③ **顺序只有一个来源** | `Stage()`；`OpenGLRenderer::Init()` 里按它 `stable_sort` |

带来的直接好处：`OpenGLRenderer.h` 里**一个 `unsigned int` 纹理句柄都没有了**（详见 7.4 的对照）。

### 5.2 Pass 基类与帧上下文（`Passes/OpenGLRenderPass.h`）

```cpp
class OpenGLRenderPass {
public:
    virtual ~OpenGLRenderPass() = default;
    virtual bool Setup() = 0;                          // 只调一次；false = 初始化失败
    virtual void OnResize(int w, int h) {}             // 尺寸变了才调（默认空实现）
    virtual void Execute(OpenGLRenderContext& ctx) = 0; // ★ 非 const：要往 ctx 写跨 Pass 资源
    virtual RenderPassStage Stage() const = 0;          // 排序用
};
```

`OpenGLRenderContext` 是**每帧一个**的"数据总台"：

```cpp
struct OpenGLRenderContext {
    const std::vector<RenderCommand>* commands;   // renderer 填（已排好序）

    int fbWidth, fbHeight;                        // renderer 填（谁改 glViewport 谁负责用这个恢复）

    // ---- 每帧一次的帧数据（renderer 填）----
    glm::mat4 viewMatrix, projectionMatrix;
    glm::vec3 cameraPos, lightPos, lightColor;

    // ---- 跨 Pass 的产物（写前在的 Pass 填，后面的 Pass 读）----
    glm::mat4    lightSpaceMatrix;  unsigned int shadowMapTex;
    unsigned int gbufferNormalTex;  unsigned int gbufferDepthTex;
    int          gbufferWidth, gbufferHeight;
    unsigned int ssaoRawTex;        unsigned int ssaoTex;
    int          ssaoWidth, ssaoHeight;
    unsigned int screenShadowRawTex; unsigned int screenShadowTex;
    int          screenShadowWidth, screenShadowHeight;
    unsigned int ssgiRawTex;        unsigned int ssgiTex;   // raw = 带噪点；tex = 去噪后
    int          ssgiWidth, ssgiHeight;
};
```

> **为什么用具名字段，而不是 `map<string, Resource>` 的"命名槽位"**：
> 具名字段拼错是**编译期**报错，而且类型安全（纹理句柄和尺寸不会写反）；
> 命名槽位要等到运行时取出来才发现拼错了。当前这个规模，具名字段更划算。

### 5.3 Stage：唯一的顺序来源

```cpp
enum class RenderPassStage : int {
    Shadow           = 100,   // 灯光空间深度图
    GBuffer          = 200,   // 世界法线 / 世界深度（MRT）
    AO               = 300,   // SSAO（全屏）
    AOBlur           = 310,   // AO 磨平（必须另起一趟）
    ScreenShadow     = 350,   // 屏幕空间 PCSS（依赖 G-Buffer + shadow map）
    ScreenShadowBlur = 360,   // PCSS 去噪（必须另起一趟）
    SSGI             = 370,   // ★ 必须在 ScreenShadow 之后
    SSGIBlur         = 371,   // SSGI 去噪（必须另起一趟）
    Opaques          = 500,   // BasePass
    Transparents     = 600,   // ⬜ 待做
    PostProcess      = 800,   // ⬜ 待做
};
```

**两条顺序不是随便定的**：

1. **`AOBlur` / `ScreenShadowBlur` / `SSGIBlur` 必须晚于它们的上游，而且是另一个 Pass** ——
   模糊要读"邻居的值"，而那个值正是上游 Pass 要写的纹理 → **同一张纹理既读又写 = 反馈循环**。
   所以只能拆成两张纹理、两趟。
2. **`SSGI(370)` 必须晚于 `ScreenShadowBlur(360)`** —— 屏幕空间步进判定"命中"的条件是
   `sampleDist ≈ sceneDist`，也就是说**命中点就是 `uv_s` 那个像素上可见的那块几何本身**。
   既然是同一个点，在那个 UV 上采样屏幕空间阴影就是它的可见性，不会错位。
   缺了它，SSGI 会把"阴影里的几何"也当成光源。

### 5.4 一帧的执行流程

```
renderer->ExecuteRenderCommands(commands, cameraData)
  │
  ├─① glEnable(GL_MULTISAMPLE)          // TODO(S8)：一次性设置，应该挪到 CreateWindow()
  ├─② 填 ctx：commands / fb 尺寸 / view / projection / cameraPos / lightPos / lightColor
  ├─③ 尺寸变了吗？变了就 for (pass) pass->OnResize(w, h)
  └─④ for (pass : mPasses) pass->Execute(ctx)      ← 就这一行，加新 Pass 不用改这里
```

各 Pass 自己做的事（按执行顺序）：

| Pass | Stage | 切到哪张 RT | 读 ctx | 写 ctx |
|---|---|---|---|---|
| `ShadowPass` | 100 | 自己的 1024² 深度图 | `lightPos`、`commands` | `lightSpaceMatrix`、`shadowMapTex` |
| `GBufferPass` | 200 | 全屏 MRT（法线 + 世界深度） | `view/projection`、`cameraPos`、`commands` | `gbufferNormalTex`、`gbufferDepthTex`、尺寸 |
| `SSAOPass` | 300 | 全屏 R8 | G-Buffer 两张 + `view/projection` | `ssaoRawTex` |
| `SSAOBlurPass` | 310 | 全屏 R8 | `ssaoRawTex`、`gbufferDepthTex` | `ssaoTex`、尺寸 |
| `ScreenShadowPass` | 350 | 半分辨率 R8 | G-Buffer、`shadowMapTex`、`lightSpaceMatrix` | `screenShadowRawTex`、尺寸 |
| `ScreenShadowBlurPass` | 360 | 半分辨率 R8 | `screenShadowRawTex`、`gbufferDepthTex` | `screenShadowTex` |
| `SSGIPass` | 370 | 半分辨率 RGBA16F | G-Buffer、`screenShadowTex`、光源 | `ssgiRawTex`、尺寸 |
| `SSGIBlurPass` | 371 | 半分辨率 RGBA16F | `ssgiRawTex`、G-Buffer 两张（当权重） | `ssgiTex`、尺寸 |
| `BasePass` | 500 | **默认 FBO（窗口）** | `commands`、`screenShadowTex`、`ssaoTex`、`ssgiTex`、`screenSize` | —（这是最终画面） |

**每个全屏 Pass 的收尾动作都是同一套**（漏一个就会出怪问题）：

```cpp
glBindFramebuffer(GL_FRAMEBUFFER, 0);                          // 切回主帧缓冲
glViewport(0, 0, ctx.fbWidth, ctx.fbHeight);                   // ★ 恢复视口
glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0); // ★ 解绑！否则下一帧形成反馈循环
glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
```

**每个全屏 Pass 的开头也都是同一套**：

```cpp
glDisable(GL_DEPTH_TEST);  glDepthMask(GL_FALSE);
glDisable(GL_BLEND);       glDisable(GL_CULL_FACE);
```

> 全屏 Pass 为什么必须显式关掉这些：① 它们本来就没意义（没有深度附件）；
> ② 更重要的是**如果留着上一批物体设的深度测试，全屏三角形的片元可能被整片丢掉**
> → 症状是"这个 Pass 什么都没画出来"，很难查。

**清屏为什么不用 `glClearColor`**：它是**全局状态**，在 G-Buffer Pass 里一改，
下一帧主帧缓冲的 `Clear()` 就被污染了。所以全屏 Pass 一律用
`glClearBufferfv(GL_COLOR, N, value)` / `glClearBufferfv(GL_DEPTH, 0, &one)` ——
直接指定"清哪个附件、清成什么值"，完全不碰全局状态。

### 5.5 加一个新 Pass 要做什么

1. 在 `Passes/` 下建 `XxxPass.h/.cpp`，继承 `OpenGLRenderPass`。
2. 实现 `Setup()`（建 GPU 资源，返回 `false` 表示失败）、`Execute(ctx)`、`Stage()`；
   屏幕尺寸的 RT 要额外实现 `OnResize(w, h)`，并在析构里删干净。
3. 在 `RenderPassStage` 里选一个数值（可以复用相邻档位中间的数，比如 371）。
4. 在 `OpenGLRenderer::Init()` 里 `mPasses.emplace_back(std::make_unique<XxxPass>())`。
5. 把 `.h/.cpp` 写进 `CMakeLists.txt`，shader 放进 `src/shaders/`。
6. **不需要动 `ExecuteRenderCommands`，也不需要动别的 Pass。**
7. 需要跨 Pass 传数据？**在 `OpenGLRenderContext` 里加一个具名字段**，
   写前在的 Pass 填、后面的 Pass 读。不要在 Pass 之间互相持有指针。

> **`OnResize` 的时机坑（已处理）**：`Setup()` 拿不到窗口尺寸，所以屏幕尺寸的 RT 建不出来。
> `OpenGLRenderer::Init()` 在 `Setup()` 之后**立刻补调一次 `OnResize`**，
> 保证第一帧之前所有 Pass 的资源都是齐的 —— 省得 `Execute` 里到处判"RT 建好了吗"。

### 5.6 顺序之外的"阶段内"逻辑

`stable_sort` 保证同 Stage 的 Pass 不被重排，所以想插到两档中间（比如 371）也安全。
但要注意 **Pass 之间的隐式依赖**：`ScreenShadowPass` 和 `SSGIPass` 都在 `Execute` 里
用 `if (ctx.xxx == 0) return;` 跳过自己 —— 上游没产出就安静地不做，
这样即使某个 Pass `Setup` 失败也不会连累后面的（代价是"没效果"和"算错了"看起来一样，
所以每个 Pass 都留了调试自检，见 6.6）。

---

## 六、屏幕空间三件套：AO / PCSS / SSGI

这三个效果的骨架**是同一个套路**，理解一个就会另外两个：

```
   G-Buffer（世界法线 + 世界深度）
        │
        ├─→ SSAOPass ──────→ SSAOBlurPass ────┐
        │   32 个半球采样      5×5 深度加权    │
        │                                      │
        ├─→ ScreenShadowPass → ScreenShadowBlurPass ──→ SSGIPass → SSGIBlurPass
        │   PCSS（blocker     双边模糊          │        命中点的可见性   双边去噪
        │    search + 可变半径 PCF）             │        算间接光
        │                                      │
        └──────────────────────────────────────┴──→ BasePass：各采样一次
```

### 6.1 G-Buffer：世界法线 + 世界深度（`GBufferPass`）

**一个 FBO、三个附件**：

| 附件 | 格式 | 内容 |
|---|---|---|
| `COLOR_ATTACHMENT0` | `RGBA16F` | 世界法线（xyz），w 留给以后放粗糙度/切线 |
| `COLOR_ATTACHMENT1` | `R32F` | **世界深度** = 点到相机的距离（世界单位） |
| `DEPTH_ATTACHMENT` | Renderbuffer `DEPTH24` | 只给光栅化做遮挡判断用，**不需要被采样** |

**两个关键决定**：

1. **深度分两份**：硬件深度缓冲是 `[0,1]` 的**非线性**值，做 AO 时几乎总要线性化和反算；
   而"点到相机的距离"本身就是世界单位，读的人（AO / PCSS / SSGI）拿来直接用。
   代价是多一次带宽，好处是省掉一整套 near/far 反算。
2. **`glDrawBuffers` 不能忘**：默认只有 `COLOR_ATTACHMENT0` 是打开的，
   不写这两行的话 `gDepth` **永远写不进去**（而且不报错，只是全 0）。

```cpp
const GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
glDrawBuffers(2, drawBuffers);
```

**背景像素的哨兵值**：清屏时世界深度写成 `kNoGeometryDepth = 100.0f`，
读的人用 `dist >= FAR_DIST`（90 左右）判断"这里没有几何"。
法线则清成 `(0,0,0)`，读的人用 `dot(N, N) < 0.1` 判断。

**法线必须用逆转置矩阵变换**（`gbuffer_vert.glsl`）：

```glsl
mat3 normalMatrix = transpose(inverse(mat3(ModelMatrix)));
vWorldNormal = normalize(normalMatrix * aNormal);
```

因为法线是"垂直于表面"的方向，非等比缩放会破坏垂直关系
（场景里 `plane2` 的 scale 就是 `(3,1,3)`，所以这一步不能省）。

### 6.2 SSAO：算得对，还要"合理"（`ssao_frag.glsl`）

三步：**重建世界位置 → 法线半球撒 32 个点 → 投影回屏幕比距离**。

**遮挡判定为什么可以简化成"比距离"**：采样点 S 投影到屏幕上会落在某个 UV；
那个 UV 上的几何点 P **一定和 S 在同一条视线上**。
所以 S 是否被挡住 ⟺ `S 到相机的距离 > P 到相机的距离`（+偏移），
而"到相机的距离"正是 G-Buffer 里存的东西 —— 不需要比 z，也不需要转视空间。

**但只做这一步会出问题**，所以有三个"合理性"修正（都是有物理动机的，不是调参糊过去的）：

| 修正 | 解决什么 | 怎么做的 |
|---|---|---|
| **range check** | 猴头周围那一圈假 AO | `smoothstep(0, 1, RADIUS / diff)`：按"遮挡物离采样点多远"加权，几个半径之外权重趋 0 |
| **normal check** | 采样点穿过几何打到背面 | `smoothstep(kNormalMin, 0, dot(sampleN, N))`，阈值取 **负数** `-0.25` |
| **距离淡出** | 远处 AO 又假又没用 | `FADE_START=15` → `FADE_END=45`，`mix(1.0, ao, distFade)` |

> **`kNormalMin` 为什么必须是负数**：真实的墙角/凹角两个面是**互相垂直**的（`dot ≈ 0`）。
> 阈值取 0 或正数 → 把墙角本身的 AO 也一起杀掉了（那就白做了）。
> 所以只排除"明显背对着"的面（`dot < 0`），而且法线是插值出来的，要留余量。

**采样核为什么不硬编码**：用**黄金角螺旋**（3 行代码）程序化生成 ——
同时满足"半球上均匀分布"（低差异，少噪点）和"越靠近原点越密"（近处遮挡更重要）。
再用 `Hash21` 给每像素一个随机旋转角，把规律性花纹打散成噪点。

**模糊必须是单独一趟**（`SSAOBlurPass`）：模糊要读邻居的 AO，而那正是要写的纹理。
而且是**深度加权**（双边）而不是普通盒子模糊 —— 否则物体边缘两侧的 AO 会被混在一起，
物体周围出现一圈亮边（halo）。

> **深度权重的尺度最容易设错**：同一个表面上相邻像素的深度差约 `0.05 ~ 0.3`，
> 跨过物体边缘会跳到 `1.0` 以上。所以阈值要用**随距离自适应**的 `max(0.05, centerZ * 0.02)`。
> 写成固定值（比如 25）会导致 `exp(-0.05*25)=0.29` → 权重几乎全是 0 → 等于完全没模糊。

### 6.3 PCSS：从"每个材质像素都算"搬到"屏幕空间算一次"

**原来是**：`basicfrag.glsl` 里每个像素都跑一遍 blocker search + 可变半径 PCF（约 70 行、32 次采样）。
三个问题：a) 同一片像素被多个物体重复覆盖时会重复算；b) 材质着色器越来越重；
c) 没法整体降分辨率、也没法单独给阴影做去噪。

**现在是**：`ScreenShadowPass` 对屏幕上每个**可见**像素算一次，写进一张纹理，
`basicfrag` 那边只做**一次**采样。

```glsl
// 三步（screen_shadow_frag.glsl）
① FindBlockerDepth  // 搜索圈里遮挡物的平均深度（16 点，负数 = 没有遮挡物）
② PenumbraSize      // (receiverDepth - blockerDepth) / blockerDepth
③ ShadowFactorPCSS  // radius = clamp(size * lightTexel, 1, searchTexel) 的可变半径 PCF
```

| 参数 | 值 | 说明 |
|---|---|---|
| `PCF_SAMPLES` | 16 | 泊松盘 |
| `kPCSSBias` | `0.002` | 深度比较偏移 |
| `kPCSSSearchTexel` | `10.0` | blocker search 半径（纹素），也是可变半径的上限 |
| `kPCSSLightTexel` | `5.0` | 半影放大系数 |
| `kPCSSBlockerEps` | `0.02` | 判定"这是遮挡物"的深度阈值 |
| 分辨率 | **半分辨率**（`kDownscale = 2`） | 阴影是低频的，上采样靠 `GL_LINEAR` 自然完成 |

**为什么必须再有一趟双边模糊**：blocker search 用 16 个点估"遮挡物平均深度"，
这个估计是**抖的** → 半影半径在相邻像素之间跳变 → 阴影边缘斑驳。

**泊松盘而不是 3×3 / 4×4 网格**：规则网格的采样点与 texel 网格同向 → 出现**结构化的条带/阶梯伪影**；
泊松盘"随机但保持最小间距" → 不聚集、不留空洞、不跟纹素网格对齐。

**三个坑**：
1. `radius` 的单位是**纹素**，忘了乘 `texelSize` 半径就变成"半个屏幕"，阴影糊成一片。
2. 数组必须是 `const` 且大小是**编译期常量** —— 这是编译器能把循环**展开**的前提。
3. 采样点会跑到 `[0,1]` 之外，靠 `GL_CLAMP_TO_BORDER`（border=1.0=被照亮）兜底，所以**视锥边缘不会出现假阴影**。

### 6.4 SSGI：把 0.2 那个常数换成真的算出来的环境光（`ssgi_frag.glsl` + `ssgi_blur_frag.glsl`）

**和 SSAO 的本质区别**（这是理解 SSGI 的关键）：

| | 问的问题 | 得到 | 数学性质 |
|---|---|---|---|
| **SSAO** | 这根射线**被挡住没有** | `[0,1]` 标量 | **只能减光** |
| **SSGI** | 挡住它的那个表面**自己有多亮** | 颜色/亮度 | **可以加光** —— 这才是 GI |

算法（逐像素，`ssgi_frag.glsl`）：

```
① 重建世界位置 + 法线 + TBN（和 SSAO 完全同一套推导）
② 在法线半球内取 24 个【余弦加权】方向，起点沿法线抬起 kOriginLift 躲开自交
③ 每根射线做 24 步屏幕空间步进（近密远疏），一旦"钻到某个像素的几何后面"就算命中
④ 命中点的辐射 ≈ 它自己的直接光照 = 光色 × N·L × 可见性
⑤ 按几何衰减 1/(1+r²) 累加，除以射线数 → 写进 ssgiRawTex
⑥ SSGIBlurPass 再起一趟做「深度 + 法线」加权双边模糊 → ssgiTex
```

| 参数 | 值 | 说明 |
|---|---|---|
| `RAY_COUNT` | 24 | 每像素射线数（噪点 ∝ `1/√N`） |
| `STEP_COUNT` | 24 | 每根射线的步进数 |
| `RAY_LENGTH` | `3.5` | 射线最远走多远（世界单位）—— 决定"能看见多远的光" |
| `kMinStep` | `0.15` | 第一步至少走多远（躲开近场自交） |
| `kOriginLift` | `0.04` | ★ 射线起点沿**法线**抬起（见下） |
| `kThicknessMin` / `kThicknessStepScale` | `0.05` / `2.0` | 厚度上界（见下） |
| `kMaxIndirect` | `vec3(1.5)` | 限幅（firefly 抑制） |

**四个"合理性"修正**（和 SSAO 一样，都是为了让结果在几何上说得通，而不是调参遮丑）：

| 修正 | 解决什么 | 怎么做的 |
|---|---|---|
| **起点抬起** | 射线一出门就撞到自己 | `rayOrigin = worldPos + N * kOriginLift` —— 抬起之后采样点永远在表面**靠近相机的一侧**，到相机的距离比表面小 → 不会自交 |
| **近密远疏的步长** | 近处的反弹最有信息量，远处本来就该糊 | `t = kMinStep + (RAY_LENGTH - kMinStep) * u²`（`u = s/STEP_COUNT`，`u²` 让步长随距离线性增长） |
| **厚度上界** | 擦过轮廓时误判成命中（会把轮廓另一侧的远物体当成反弹源） | `thickness = max(stepLen * kThicknessStepScale, kThicknessMin) + bias`；`diff >= thickness` → 认为是"跨过轮廓"，继续走 |
| **背面剔除** | 命中面背着接收者，它只会**遮挡**、不会发光 | `if (dot(Nh, -dir) <= 0.0) break;`（`-dir` 就是"命中点 → 接收者"的方向） |

**命中位置要做线性插值**：命中发生在"上一步"和"这一步"之间。
不插值的话 `hitT` 只能取到 24 个离散值 → 衰减 `1/(1+r²)` 跳变 → 变成**结构性噪点**（比随机噪点更难去）。
所以用 `w = prevDiff / (prevDiff - diff)` 在 `prevT` 和 `t` 之间 `mix` 出真正的穿越点。

**为什么可以复用屏幕空间阴影判断命中点的可见性**：屏幕空间步进判定"命中"的条件是
`sampleD ≈ sceneD`，也就是说**命中点就是 `uv_s` 那个像素上可见的那块几何本身**。
既然是同一个点，在那个 UV 上采样屏幕空间阴影当然就是它的可见性，不会错位。
（代价：那张阴影是半分辨率 + 模糊过的，所以可见性是"软"的 —— 对 GI 刚好够用。）

**为什么用余弦加权采样**：要算的是 `E = ∫ L(ω)·cosθ dω`。
按 `pdf = cosθ/π` 采样，估计量变成 `(1/N)Σ L·cosθ/pdf = (1/N)Σ L·π` —— **cosθ 被消掉了**。
也就是说接收者的 N·L 项**不用显式乘**，采样本身已经包含它，而且方向自然集中在法线附近。

**为什么必须再去噪一趟**（`ssgi_blur_frag.glsl`）：24 根射线 × 每根只命中一次 → 估计量方差很大，看起来是一层颗粒。
单纯加射线数的收益只有 `√N`（射线翻 4 倍才把噪点减半），代价却线性涨 —— **空间去噪才是性价比最高的一步**。

它是 **9×9**（比 AO 的 5×5 大，因为 GI 的噪点颗粒更大）、**双权重**：

| 权重 | 式子 | 解决什么 |
|---|---|---|
| **深度** | `exp(-abs(z - centerZ) / max(0.05, centerZ * 0.02))` | 轮廓两侧深度差很大 → 权重趋 0 → 不会把物体的间接光糊到背景上（那是假 halo） |
| **法线** | `max(dot(n, centerN), 0)⁴` | 弯曲表面（猴头）上相邻像素**深度很接近**，光靠深度分不出"同一个面"和"隔着一个折角的两个面"，法线能把它们分开 |

**怎么接到最终画面上**（`basicfrag.glsl`）：

```glsl
vec3 ambient = vec3(0.2) + indirect;   // 常数兜底 + 屏幕空间间接光
fragColor = vec4(diffuse * shadow * 0.8 + ambient * ao, 1.0);
```

分工是：**AO 管"这里该不该暗"，SSGI 管"光从哪儿来"**，两者一起被 AO 削弱。

**关掉时为什么画面完全一样**：不是"跳过整个 Pass"，而是把纹理**清成 0**。
这样 `BasePass` 可以**无条件**地 `+ indirect` —— 关闭时 `indirect = 0`，
这一行退化成原来的 `0.2 * ao`，和加 SSGI 之前**逐像素一致**，天然是一个干净的 A/B 基准。
（★ 关掉时 `SSGIPass` 依然会发布 `ssgiRawTex`，`SSGIBlurPass` 也照常跑一遍模糊 0 —— 所以下游不需要知道开关状态。）

### 6.5 BasePass：材质着色器现在只剩 3 次采样

```glsl
// basicfrag.glsl 的 main() 全部有效内容
vec2 screenUV = gl_FragCoord.xy / screenSize;        // ★ 分母是【屏幕尺寸】
float shadow   = texture(screenShadow, screenUV).r;
float ao       = texture(aoMap,        screenUV).r;
vec3  indirect = texture(ssgiMap,      screenUV).rgb;
```

**★ `screenSize` 这个坑必须记住**：屏幕空间纹理的 UV 分母只能是**屏幕尺寸**，
**不能**写成 `gl_FragCoord.xy / textureSize(阴影纹理)` ——
只有"纹理和屏幕同分辨率"时两者才相等。阴影纹理是**半分辨率**的，
用 `textureSize` 会让 UV 变成 `0~2`，画面就被缩小、贴到左下角。

**三个纹理单元的分工**（`BasePass::Execute`）：

| 单元 | 纹理 | uniform |
|---|---|---|
| 0 | `ctx.screenShadowTex` | `screenShadow` |
| 1 | `ctx.ssaoTex` | `aoMap` |
| 2 | `ctx.ssgiTex` | `ssgiMap` |

**画完必须全部解绑**（`glBindTexture(GL_TEXTURE_2D, 0)`）：
否则下一帧 `ShadowPass` 把同一张纹理当**深度附件**渲染时，
就形成"同一张纹理既被写、又被采样"的反馈循环（原来 S2 报的就是这个）。

> 对没有声明该 uniform 的 shader（比如 `groundNetFrag` 没有 `aoMap`）调用无害：
> `glGetUniformLocation` 返回 `-1`，而 `glUniform*` 在 location 为 `-1` 时**被规范要求忽略**。

### 6.6 调试手段：让"看不见的中间结果"可见

G-Buffer / AO / 阴影 / 间接光都**不显示在屏幕上**，所以"没效果"和"算错了"看起来一模一样。
每个 Pass 都留了一段**读回并打统计**的自检代码（默认只打印一次，不会刷屏）：

| Pass | 开关 | 打印什么 | 怎么判断 |
|---|---|---|---|
| `GBufferPass` | `kDumpGBuffer = false` | 中心 64×64 块的平均法线、世界深度 min/max/avg | 有几何的像素数应该接近块大小 |
| `SSAOPass` | `kDumpSSAO = true` | 最低/平均 AO、被遮挡像素占比 | 猴头凹陷处应明显 `< 1`，平面≈1 |
| `ScreenShadowPass` | `kDumpScreenShadow = true` | 最暗值、平均、阴影占画面比例 | 阴影占画面几个百分点是合理的 |
| `SSGIPass` | `kDumpSSGI = true` | 有间接光的像素占比、最亮、平均亮度 | **全 0 = 射线一根都没命中** |
| `SSGIBlurPass` | 无 | — | 它的输入就是上面那张，所以先看 `SSGIPass` 的输出即可 |

**看够了就把对应开关改成 `false`**（见 11.6 清理项）。

**另一种更直接的调试法**：把中间纹理当灰度图打出来 ——
比如临时把 `basicfrag` 的 `fragColor` 改成 `vec4(vec3(texture(screenShadow, screenUV).r), 1.0)`：
看到**猴头的白色剪影**说明 Shadow Pass 完全正确，问题在后面的比较逻辑；
全白/全黑/乱码说明问题在 Shadow Pass 本身（灯光矩阵、视口、FBO）。

还可以把不同量分通道打出来，一次分辨是谁的问题：
```glsl
fragColor = vec4(lambert, shadow, ao, 1.0);   // R=光照项, G=阴影, B=AO
```

### 6.7 踩过的三个坑（值得记住）

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
这也是 `ShadowPass` 现在每帧从 `ctx.lightPos` 重算灯光矩阵的原因 ——
光源只有一个真相源（`OpenGLRenderer::lightPos`）。

---

## 七、文件清单

### 7.1 入口与配置

---

#### `src/main.cpp` ✅

**职责**：程序入口 + 渲染主循环。仍承担了较多职责（将来会被 `Application` / `Scene` 层接管）。

**关键点**：**不含任何 OpenGL 头文件**（`OpenGLRenderer.h` 的 include 被注释掉了）。只 include `<iostream>` / `Camera.h` / `Renderer.h` / `RenderQueue.h`。

**流程**：

```
1. IRenderer* renderer = CreateRenderer(WINDOW_WIDTH, WINDOW_HEIGHT);
   if (!renderer) { 报错退出 }              // 后端没编入时的兜底
   renderer->Init()                          // 建窗口 + 上下文 + GLAD + 装配 8 个 Pass + 补一次 OnResize

2. Camera camera; camera.SetViewportSize(renderer->GetWindowSize());

3. 四份 Material：
      material      (basicvertex/basicfrag)   depthTest = true, baseColor = 白   → 猴头
      material2     (同一个 shader)            depthTest = true, baseColor = 红   → plane2
      material3     (同一个 shader)            depthTest = true, baseColor = 绿   → plane3
      planeMaterial (groundNet*)              AlphaBlend, depthWrite=false     → 半透明网格地面
   （★ material / material2 / material3 共用同一个 shader 程序，只差一个 baseColor uniform
     —— 这正是"材质 = shader + 状态 + 参数"的用法：换颜色不用换 shader。）

4. 四份 Mesh：LoadObj → renderer->CreateMesh() → SetData(...)

5. 组装队列（★ 提交顺序故意是反的，用来验证排序生效）：
      Submit(plane ...)      // 半透明，先提交（应该被排到后面）
      Submit(mesh  ...)      // 不透明
      Submit(plane2...)      // 不透明，位置 (0,-1,0)、缩放 (3,1,3)
      Submit(plane3...)      // 不透明，位置 (0,0,90)、缩放 (1.5,1,1.5)

6. 主循环：
      PollEvents → camera.mouseX += 1.0（调试自转）→ camera.Update()
      → RenderQueue::Sort(cameraData.position)     // ★ 每帧排序
      → renderer->Clear()
      → renderer->ExecuteRenderCommands(queue.Commands(), cameraData)
            // 内含 8 个 Pass；main 完全不知道有几个 Pass
      → SwapBuffers
```

**依赖**：`Renderer.h`、`RenderQueue.h`、`Camera.h`、`Material.h`、`ObjLoader.h`、`<iostream>`
**被谁使用**：无（程序入口）

**已知问题**：
- 大量死代码：`vertices[]`、`indices[]`、局部 `view` / `projection` / `aspect` / `OrthoProjectionMatrix` / `viewWidth` / `viewHeight` 全部不再被使用
- `camera.mouseX += 1.0` 是调试代码，导致相机每帧持续自转（代码里注释写着"临时冻住相机（验证颜色渗透）"，其实是在自转）
- `shader->BuildFromFiles(...)` 的**返回值被丢弃** → shader 加载失败时静默继续（N8）
- **`plane3` 没有 `delete`** → 泄漏（M1；`plane2` 已经补上了）
- **`plane2->SetData(objMeshData1)` / `plane3->SetData(objMeshData1)`** 用的都是**第一个平面**的数据，不是 `objMeshData2`（M2；三个 `plane.obj` 内容相同所以看不出问题，但 `objMeshData2` 白加载了）
- 两条 `LoadObj` 失败提前 `return -1` 的路径上，已 new 的 shader/mesh/renderer 没有释放（M3）
- 结尾的 `delete` 顺序是正确的：**先删资源、最后删 renderer**（因为 `~OpenGLRenderer` 会 `mPasses.clear()` → 再 `glfwTerminate`）

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

**职责**：现在只剩两样东西 —— `Transform` 结构，和一个 `TransformToModelMatrix`。

```cpp
struct Transform { glm::vec3 position; glm::vec3 rotation; glm::vec3 scale; };

inline glm::mat4 TransformToModelMatrix(const Transform& transform);   // T→R(x,y,z)→S（每步后乘）
```

```cpp
inline glm::mat4 TransformToModelMatrix(const Transform& transform){
    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, transform.position);
    m = glm::rotate(m, transform.rotation.x, glm::vec3(1,0,0));
    m = glm::rotate(m, transform.rotation.y, glm::vec3(0,1,0));
    m = glm::rotate(m, transform.rotation.z, glm::vec3(0,0,1));
    m = glm::scale(m, transform.scale);
    return m;
}
```

**关键点**：头文件里的**函数体**必须加 `inline`。
不加的话，每个 include 它的 `.cpp` 都会生成一份**强定义**（`nm` 里是 `T`），链接期直接报
`multiple definition of 'TransformToModelMatrix(Transform const&)'`。
加了 `inline` 变成**弱符号**（`nm` 里是 `W`），链接器自动合并。

> ★ Pass 化之后这个函数被**调用的次数变多了**（`ShadowPass`、`GBufferPass`、`BasePass` 各自算一遍），
> 但这是故意的：`RenderCommand` 只存 `Transform`，model 矩阵在**用它的那个 Pass 里**现算，
> 避免同一个 model 出现两个真相源。

**已知问题**：
- ✅ **已修**：两个 `GetLightSpace*Matrix`（死代码，`lightDir` 参数还完全没用过）已经删除。
  灯光矩阵现在只有 `ShadowPass::UpdateLightMatrices()` 一处，每帧从 `ctx.lightPos` 重算
- 🚧 `Transform` 是场景/渲染概念，却住在名字叫 `MatrixTools` 的文件里，命名不贴切（N10）

---

### 7.2 抽象接口层

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

    // ---- 通用 uniform setter：各屏幕空间 Pass / Shadow Pass 用 ----
    virtual void SetMat4(const std::string& name, const glm::mat4& value) = 0;
    virtual void SetInt (const std::string& name, int value) = 0;
    // 屏幕空间 Pass 需要知道"屏幕有多大"才能把 gl_FragCoord 换算成 [0,1] 的 UV。
    // ★ 必须传屏幕尺寸，不能拿 textureSize(屏幕空间纹理) 当分母：
    //   降分辨率后 textureSize 只有一半，UV 会变成 0~2 → 画面缩小贴到左下角。
    virtual void SetVec2(const std::string& name, const glm::vec2& value) = 0;
    virtual void SetVec3(const std::string& name, const glm::vec3& value) = 0;   // 材质参数（baseColor）
};
```

**依赖**：`<string>`、`<glm/glm.hpp>`、`<glm/gtc/type_ptr.hpp>`（**不依赖任何图形 API 头**，干净的一层）

**为什么是通用 setter，而不是 `SetTexture(ITexture*)`**：
纹理绑定（`glActiveTexture` / `glBindTexture`）由各 Pass 直接做（它们本来就是 OpenGL 层）；
shader 只需要知道"采样器 uniform 指向第几个纹理单元"，那就是一个 `int`。
这样**不需要新增 `ITexture` 抽象**，而且这几个方法对 Vulkan 后端也成立。

> ⚠️ **千万别加 `SetTextureID(unsigned int)` / `GetDepthTextureID()` 这类接口** —— 那等于把 `GLuint` 塞进抽象层。
>
> ⚠️ **通用 setter 是"逃生舱"，不是默认路线**：能用语义化 setter（`SetMatrix` / `SetLight` / `SetCamera`）
> 表达的就不要用 `SetMat4("ModelMatrix", ...)` —— 后者把 uniform 名字这个"约定"散到了各个 Pass 里，
> 拼错了只会静默失效（`glGetUniformLocation` 返回 -1 不报错）。

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
**被谁实现**：`OpenGLMesh` —— **被谁使用**：`RenderCommand`、`BasePass`、`ShadowPass`、`GBufferPass`、`main.cpp`

**已知问题**：`vertexCount` 的语义其实是"**float 个数**"（`OpenGLMesh` 里用它 × `sizeof(float)` 传给 `glBufferData`），名字有歧义（M5）

---

### 7.3 渲染核心层

---

#### `src/Render/Renderer.h` ✅

**职责**：`IRenderer` 抽象接口 + `BuiltInRendererFeatures` 枚举 + `CreateRenderer` 的**声明**。

```cpp
class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual glm::vec2 GetWindowSize() = 0;

    virtual bool  Init() = 0;                       // 建窗口 + 上下文 + GLAD + 装配 Pass 管线
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
                                       const CameraData& cameraData) = 0;   // 内含全部 Pass
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
- 想在两个 Pass 用不同画法 → **换一份 Material 就行**；而 `ShadowPass` / `GBufferPass` 干脆不用材质，
  它们有自己的专用着色器（见 9.2）

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

    // ★ 只描述"这个材质长什么样"，不认识任何 uniform 名
    glm::vec3 baseColor = glm::vec3(1.0f);

private:
    IShader* m_Shader = nullptr;     // ★ 不拥有所有权，只引用
};
```

**`baseColor` 是怎么到 GPU 的**：`BasePass` 每画一个物体就
`shader->SetVec3("baseColor", command.material->baseColor)`，片元着色器最后
`fragColor = vec4((diffuse * shadow * 0.8 + ambient * ao) * baseColor, 1.0)`。

> ★ 注意这里的分工：`Material::baseColor` 是**平台无关的语义数据**（一个 `glm::vec3`，没有 GL 类型），
> 而"uniform 叫什么名字"这件事被留在了 `BasePass` 里（它本来就是 OpenGL 层）。
> 这正是通用 setter 该出现的位置 —— 抽象层不出现 `GLuint`，但也不假装自己知道 uniform 名。

**为什么 `material` / `material2` / `material3` 能共用一个 shader 程序**：
"材质"= shader + 渲染状态 + **参数**。换颜色只是换参数，不需要重新编译一个 program
—— 这也顺带让 `RenderQueue` 的排序第 ② 级（按 shader 分组）把它们排在一起，少切 program。

**依赖**：`IShader.h`（**不间接依赖 glad** ✅）

**⚠️ 关于 `RenderState` 的默认值（重要）**

默认值被**刻意**设成了 **OpenGL 的真实初始状态**：`depthTest = false`、`cullMode = Off`、`blend = Opaque`。

**代价**：`Material` 的默认状态是**"不测深度、不剔面"**。所以**新建材质时一定要显式写全状态**：
```cpp
material.renderState.depthTest = true;    // 不写的话，默认是不测深度的！
```

**已知问题**：构造函数建议加 `explicit`（N11）；缺 `castShadow` / `receiveShadow` / `inGBuffer` 这类"用途"标志（S4）

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
- `aspectRatio` 是 public 而 `viewportSize` 是 private，风格不一致（C6）

---

### 7.4 OpenGL 实现层

---

#### `src/Render/OpenGL/OpenGLRenderer.h` / `.cpp` ✅

**职责**：`IRenderer` 的 OpenGL 实现。**★ Pass 化之后它只剩三件事**：

1. 建窗口 / 上下文 / 加载 GLAD（`CreateWindow()`）；
2. 装配 Pass 列表（`Init()`），并按 Stage 排序；
3. 每帧填 `OpenGLRenderContext` + 通知 `OnResize` + 按顺序跑一遍 Pass（`ExecuteRenderCommands`）。

**公开成员**：

| 成员 | 说明 |
|---|---|
| `GLFWwindow* window` | public 的窗口句柄（建议改 private） |
| `Init()` | `CreateWindow()` 成功之后 → 建 8 个 Pass → `stable_sort` → 逐个 `Setup()` → **补一次 `OnResize`** → 打印 SSGI 开关状态 |
| `GetWindowSize()` | ★ **实时** `glfwGetFramebufferSize`（原来的"固定值"问题 C5 已修） |
| `Clear()` | 先 `glDepthMask(GL_TRUE)` + 同步缓存，再 `glClear(颜色 \| 深度)` ✅ |
| `PollEvents()` | `glfwPollEvents()` + **按 G 切换 SSGI**（带边沿检测，见下） |
| `CreateShader()` / `CreateMesh()` | 抽象工厂 |
| `ExecuteRenderCommands(...)` | 填 ctx → OnResize → 跑 Pass。**不含任何具体渲染逻辑**（见 5.4） |

**私有的状态系统**：

| 成员 | 说明 |
|---|---|
| `mRenderState` | ⚠️ **现在只剩 `Clear()` 用它的 `depthWrite`** —— 按材质应用状态（`ApplyRenderState`）已经搬到 `BasePass.cpp` 的文件级匿名命名空间里了 |

> **⚠️ 状态缓存现在是"半废弃"状态**：`BasePass` / `ShadowPass` / 各全屏 Pass 都选择**无条件发状态、不做缓存**，
> 理由是"GL 状态是全局的，而缓存成立的前提是缓存与 GL 真实状态严格一致"，
> 多个 Pass 各存一份缓存必然互相打架（这个坑踩过两次：`Clear()` 的 depth mask、ShadowPass 的 depth mask）。
> **正解**是把缓存抽成一个**共享的** `OpenGLStateCache`，由 renderer 持有、放进 ctx 让所有 Pass 共用 ——
> 那样缓存只有一份才可能保持一致。在那之前，物体这么少，几次多余的 `glEnable` 完全无感。（见 11.4 N15）

**Pass 管线相关的私有成员**：

| 成员 | 说明 |
|---|---|
| `std::vector<std::unique_ptr<OpenGLRenderPass>> mPasses` | ★ 全部渲染逻辑都在这里 |
| `SSGIPass* mSSGI` | **非拥有**指针，只为"按 G 切换"用；★ 必须在 move 进 vector **之前** `.get()` |
| `bool mSSGIKeyHeld` | 按键边沿检测 |
| `int mLastFBWidth / mLastFBHeight` | 尺寸变了才通知各 Pass（避免每帧都调 `OnResize`） |
| `const glm::vec3 lightPos = (1, 2, 0.4)` | 光源位置（唯一真相源；`ShadowPass` 每帧从 ctx 读它重算灯光矩阵） |
| `glm::vec3 lightColor` | 光源颜色 |

**★ 已删除**（这就是 Pass 化的收益）：`mShadowShader`、`depthTex`、`shadowFBO`、`lightProjection`、
`lightView`、`InitShadowPass()`、`ApplyRenderState()`、`RenderStateToOpenGL()`、
以及 GBuffer 的那几张纹理 —— **`OpenGLRenderer.h` 现在一个 GPU 句柄都不持有**。
换来的是一行 `for (auto& pass : mPasses) pass->Execute(ctx);`。

**按键边沿检测为什么必须做**：

```cpp
const bool gDown = (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS);
if (gDown && !mSSGIKeyHeld) { mSSGI->SetEnabled(!mSSGI->Enabled()); ... }
mSSGIKeyHeld = gDown;
```

`glfwGetKey` 返回的是"**现在有没有按住**"，不是"这一帧刚按下"。
直接写 `if (按住) 翻转` 是错的：按住 G 100ms ≈ 6 帧 → 连翻 6 次，松手之后开关状态看起来是随机的。

**已知问题**：见 11.1（S3~S16 里的 OpenGLRenderer 相关项）、11.2（R1/R2/R5/R6）

---

#### `src/Render/OpenGL/Passes/` ✅（★ 新增，8 个 Pass）

| 文件 | 职责 | 持有的 GPU 资源 | Stage |
|---|---|---|---|
| `OpenGLRenderPass.h` | 基类 + `OpenGLRenderContext` + `RenderPassStage` | — | — |
| `FullscreenQuad.h/.cpp` | 全屏三角形（空 VAO + `glDrawArrays(3)`，顶点由 `gl_VertexID` 生成） | VAO | — |
| `ShadowPass.h/.cpp` | 灯光空间深度图 | FBO + 深度纹理 + 深度 shader | 100 |
| `GBufferPass.h/.cpp` | 世界法线 / 世界深度（MRT） | FBO + 2 纹理 + RBO + shader | 200 |
| `SSAOPass.h/.cpp` | 环境光遮蔽（32 核 + range/normal check + 距离淡出） | FBO + R8 纹理 + shader + 全屏三角形 | 300 |
| `SSAOBlurPass.h/.cpp` | 深度加权 5×5 模糊 | FBO + R8 纹理 + shader + 全屏三角形 | 310 |
| `ScreenShadowPass.h/.cpp` | 屏幕空间 PCSS（半分辨率） | FBO + R8 纹理 + shader + 全屏三角形 | 350 |
| `ScreenShadowBlurPass.h/.cpp` | 双边模糊去噪 | FBO + R8 纹理 + shader + 全屏三角形 | 360 |
| `SSGIPass.h/.cpp` | 屏幕空间全局光照（单次弹射，半分辨率，可开关） | FBO + RGBA16F 纹理 + shader + 全屏三角形 | 370 |
| `SSGIBlurPass.h/.cpp` | 给 SSGI 去噪（9×9 深度 + 法线加权双边模糊） | FBO + RGBA16F 纹理 + shader + 全屏三角形 | 371 |
| `BasePass.h/.cpp` | 用材质把队列画一遍（唯一有画面输出的 Pass） | **无**（shader/mesh 都来自 `Material`） | 500 |

**`ShadowPass` 的实际参数**（原来散在 `OpenGLRenderer` 里，现在是它自己的常量）：

| 项 | 值 | 位置 |
|---|---|---|
| 阴影图分辨率 | `kMapSize = 1024` | `ShadowPass.h` |
| 深度纹理格式 | `GL_DEPTH_COMPONENT24` + `GL_FLOAT` | `ShadowPass.cpp` |
| 过滤 | `GL_NEAREST`（阴影图**不能线性过滤**） | 同上 |
| 环绕 | `GL_CLAMP_TO_BORDER`，border = `(1,1,1,1)` = "最远深度" | 同上 |
| 灯光投影 | `glm::ortho(-5, 5, -5, 5, 1, 30)`（`kOrthoHalfSize = 5`） | `ShadowPass.cpp` |
| 灯光视图 | `glm::lookAt(ctx.lightPos, origin, up)` —— **每帧重算** | 同上 |
| 无颜色附件 | `glDrawBuffer(GL_NONE)` + `glReadBuffer(GL_NONE)` | 同上 |

> **为什么深度附件必须是 Texture 而不是 Renderbuffer**：后面的 Pass 要把它当 `sampler2D` 采样，
> 而 Renderbuffer **不能采样**。（窗口自带的那张深度缓冲就是 Renderbuffer，所以不能直接拿来用。）
>
> **为什么需要"渲染到纹理"**：相机看到的深度缓冲只有"相机能看见的表面"；
> 而阴影要回答的是"从**灯**的角度看，这个点有没有被挡住" —— 这个信息必须多渲染一遍。
> `ShadowPass` = 把"相机"换成"灯"的一次渲染到纹理。
>
> **`kOrthoHalfSize` 必须恰好包住要投影的场景**：太小 → 影子被切；
> 太大 → 深度精度浪费、边缘毛刺。

**`GBufferPass` 的资源与 `Setup`/`OnResize` 分工**：见 6.1。

**`BasePass` 为什么没有成员变量**：它不需要持有任何东西 ——
shader / mesh 都来自渲染队列里的 `Material`，相机 / 光源 / 三张屏幕空间纹理全部从 `ctx` 拿。
**这就是"用 ctx 交换数据"的好处：Pass 之间零耦合。** 它的文件里还有 `ApplyRenderState` +
3 个 `RenderStateToOpenGL` 重载（放在匿名命名空间里，不污染外部），
以及那句 `SetVec3("baseColor", command.material->baseColor)` —— **"uniform 叫什么名字"只出现在这一层**。

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
| `SetMat4(name, value)` / `SetInt(name, value)` / `SetVec2(name, value)` | 三个通用 setter（各屏幕空间 Pass 用） |

**已知问题**：
1. `BuildFromFiles` **没写 `override`**（N6）—— 靠签名一致隐式覆盖，签名写错时不会报错
2. `const mVertexPath` / `mFragmentPath` 是早期"构造时构建"留下的**死成员**（N7）
3. **每次 `Set*` 都调用 `glGetUniformLocation`**（字符串查找 + 驱动调用）。location 从链接成功那刻起就不会变，应该查一次缓存（R5）。
   ★ Pass 化之后这条**更值得修了**：现在每帧的 `Set*` 调用次数是原来的好几倍（8 个 Pass × 各自的 uniform）
4. `glUniform*` 只对**当前绑定的 program** 生效 → 所有 `Set*` **必须在 `Use()` 之后**调用
5. 构造失败无法上报：错误只能通过 `BuildFromFiles` 的返回值传递，而 main 丢弃了它（N8）
6. 编译失败时 `compileShader` 只打印日志、**仍然返回 shader 对象**，接着照样 `glAttachShader` + `glLinkProgram`
   （链接会失败，所以最终仍能靠 `GL_LINK_STATUS` 兜住，但错误信息会绕一圈）（N17）

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
现在 `VULKAN_RENDERER` 没定义所以不参与编译；一旦切过去会**链接失败**（一堆 `undefined reference`）。建议标注 `// TODO: 未实现` 或先从 CMake 清单里拿掉（S12）。

> ⚠️ **一个比"没写 `.cpp`"更根本的问题**：现在的渲染架构是 **OpenGL 专属**的 ——
> `Passes/OpenGLRenderPass.h` 里的 `OpenGLRenderContext` 直接存 `unsigned int` 纹理句柄，
> 8 个 Pass 也全部直接调 `gl*`。**这部分代码对 Vulkan 后端完全不可复用**，
> 换后端不是"补一个 `.cpp`"就完事。真要做的时候要先想清楚：
> 哪些抽象应该上移到 `Render/`（比如"一个 Pass 声明自己的输入输出资源"），哪些必须留在后端。

---

### 7.5 工具与资源

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

### 7.6 文档

| 文件 | 说明 |
|---|---|
| `src/目标渲染架构.md` | 架构规划。**⚠️ 已严重过时**：还在写 `Mesh*` / `Shader*`、`RenderQueue.h ⬜`、config.h 万能头、"下一步 1~5"（这五条现在都做完了）；**Pass 管线、G-Buffer、SSAO、屏幕空间 PCSS、SSGI 都还没写进去** |
| `src/Core/想法.md` | 随手记的疑问（"是不是该有个 Manager？""矩阵该放哪里？"） |

---

## 八、文件之间的关系

### 8.1 Include（编译期依赖）

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
                  / Passes/OpenGLRenderPass.h

OpenGLRenderer.cpp ──→ OpenGLRenderer.h
                     + 各具体 Pass 的 .h（★ 只在 .cpp 里 include，头文件保持瘦）
Render/OpenGL/Passes/*.cpp ──→ 各自的 Pass.h ──→ OpenGLRenderPass.h（ctx + Stage）
                                     │
                                     ├─→ <glad/glad.h>          ← ★ 谁用谁 include
                                     └─→ OpenGLShader.h         ← 头里只前向声明，.cpp 才 include
OpenGLShader.cpp   ──→ OpenGLShader.h ──→ IShader.h
OpenGLMesh.cpp     ──→ OpenGLMesh.h   ──→ IMesh.h ──→ ObjLoader.h
Material.cpp       ──→ Material.h ──→ IShader.h
Camera.cpp         ──→ Camera.h
ObjLoader.cpp      ──→ ObjLoader.h + tinyobjloader
```

**读法**：只有 `Render/OpenGL/` 底下的文件连到 `<glad/glad.h>` / `<GLFW/glfw3.h>`。
**`main.cpp` 已经完全不连到 glad/GLFW 了** —— 这是解耦最直观的成果。

> **两条 Pass 层的头文件纪律**（都是踩过坑总结的）：
> 1. **Pass 的头文件只做前向声明** `class OpenGLShader;`，把 `#include "OpenGLShader.h"` 留给 `.cpp` ——
>    这样 `ShadowPass.h` 就不会把整个 glad 拖给每一个 include 它的文件。
> 2. **谁用谁 include**：`.cpp` 里直接调 `gl*` / 用 `GL_*` 枚举的，必须**自己**带上 `#include <glad/glad.h>`，
>    **不能**指望"基类头里恰好 include 了 glad" —— 那样基类头一瘦身，这里就会莫名其妙报
>    `'GLenum' does not name a type`（写第一版时就踩到了）。

### 8.2 对象拥有关系（运行期）

```
main()
 ├── IRenderer* renderer ──(工厂 new / main delete)──> OpenGLRenderer
 │        │                                              ├── 拥有 → GLFWwindow
 │        │                                              ├── mRenderState（只剩 Clear() 用）
 │        │                                              └── mPasses: vector<unique_ptr<Pass>>
 │        │                                                     │
 │        │        ┌────────────────────────────────────────────┘
 │        │        │  每个 Pass 自己拥有自己的 GPU 资源（Setup 建 / 析构删）
 │        │        ├── ShadowPass          FBO + 深度纹理 + 深度 shader
 │        │        ├── GBufferPass         FBO + 法线/深度纹理 + RBO + shader
 │        │        ├── SSAOPass            FBO + R8 纹理 + shader + 全屏三角形(VAO)
 │        │        ├── SSAOBlurPass        同上
 │        │        ├── ScreenShadowPass    同上（半分辨率）
 │        │        ├── ScreenShadowBlurPass 同上（半分辨率）
 │        │        ├── SSGIPass            FBO + RGBA16F 纹理 + shader + 全屏三角形
 │        │        ├── SSGIBlurPass        FBO + RGBA16F 纹理 + shader + 全屏三角形
 │        │        └── BasePass            无（shader/mesh 都借自 Material）
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
> 3. **Pass 的资源是 RAII 的** —— `Setup()` 建、析构删，所以**不需要**在 `~OpenGLRenderer` 里手写任何释放代码。
> 4. **⚠️ 销毁顺序**：`~OpenGLShader` 会 `glDeleteProgram`、`~OpenGLMesh` 会 `glDelete*`、各 Pass 析构会 `glDeleteFramebuffers/Textures` ——
>    都**要求 GL 上下文仍存活**。所以必须：
>    ```cpp
>    // ~OpenGLRenderer()
>    mPasses.clear();      // ★ 先销毁所有 Pass（上下文还活着）
>    glfwTerminate();      // ★ 最后才销毁上下文
>    ```
>    而 `main` 那边也要**先删自己 new 的 shader/mesh、最后删 renderer**。
>    `CreateShader/CreateMesh` 出来的对象如果忘了 `delete`，`glDeleteProgram` 就永远不执行（M1）。

### 8.3 一帧的数据流

```
   main 主循环
       │
       ├─① renderer->PollEvents()             → GLFW 处理输入（+ 按 G 切 SSGI）
       ├─② camera.Update()                    → 球坐标 → View / Projection
       ├─③ RenderQueue::Sort(cameraPos)       → 不透明在前；透明按距离远→近
       ├─④ renderer->Clear()                  → 先 glDepthMask(TRUE) 同步缓存，再清颜色+深度
       │
       └─⑤ renderer->ExecuteRenderCommands(queue.Commands(), cameraData)
              │
              ├─ 填 ctx（commands / fb 尺寸 / view / projection / cameraPos / lightPos / lightColor）
              ├─ 尺寸变了 → for (pass) pass->OnResize(w, h)
              │
              └─ for (pass : mPasses) pass->Execute(ctx)     ★ 就这一行
                    │
                    ├─ ShadowPass(100)          灯光空间深度图 → ctx.shadowMapTex / lightSpaceMatrix
                    ├─ GBufferPass(200)         世界法线 + 世界深度（MRT）
                    ├─ SSAOPass(300)            32 核 → ssaoRawTex
                    ├─ SSAOBlurPass(310)        深度加权模糊 → ssaoTex
                    ├─ ScreenShadowPass(350)    PCSS（半分辨率）→ screenShadowRawTex
                    ├─ ScreenShadowBlurPass(360) 双边模糊 → screenShadowTex
                    ├─ SSGIPass(370)            单次弹射间接光 → ssgiRawTex
                    ├─ SSGIBlurPass(371)        双边模糊去噪 → ssgiTex
                    └─ BasePass(500)            逐条命令：
                          ├─ shader->Use()                        （★ 必须在所有 Set* 之前）
                          ├─ 0/1/2 号单元 ← screenShadow / aoMap / ssgiMap
                          ├─ SetVec2("screenSize", (fbWidth, fbHeight))   ★ 不是 textureSize
                          ├─ SetVec3("baseColor", material->baseColor)
                          ├─ ApplyRenderState(material->renderState)      （无条件发，不比较缓存）
                          ├─ SetMatrix(model, view, projection) + SetLight + SetCamera
                          └─ mesh->Draw()
                          （收尾：解绑 0/1/2 号纹理单元）
              │
              ▼
        renderer->SwapBuffers()
```

---

## 九、关键概念

### 9.1 渲染命令（RenderCommand）

一条命令 = **"画什么 + 用什么 + 放在哪"**。它的意义是**把"收集"和"执行"解耦**：
上层负责收集（遍历、剔除、排序），后端负责执行（逐条翻译成 GL 调用）。这样后端不需要知道"场景里有什么"。

### 9.2 材质（Material）= IShader + 渲染状态

| 数据 | 谁决定 | 变化频率 | 放哪 | 哪些 Pass 会读 |
|---|---|---|---|---|
| mesh（几何） | 物体 | 几乎不变 | `RenderCommand` | `ShadowPass` / `GBufferPass` / `BasePass` |
| transform | 物体 | 每帧可能变 | `RenderCommand` | 同上（各自现算 model 矩阵） |
| **shader 程序** | **材质** | 不变 | `Material` | 只有 `BasePass` 用它（前两个 Pass 用**自己的**专用着色器） |
| **depth / blend / cull 状态** | **材质** | 不变 | `Material` | 只有 `BasePass` 用它（`ShadowPass` / `GBufferPass` 自己写死状态） |
| view / projection | 相机 | 每帧一次 | `CameraData` → `ctx` | 所有 Pass |
| 光源位置 / 颜色 | 场景 | 基本不变 | `OpenGLRenderer` 的成员 → `ctx` | `ShadowPass`（算灯光矩阵）、`BasePass`、`SSGIPass` |

**关键在于**：**渲染状态不是"某个物体的数据"，而是"接下来这批 draw call 用什么规则画"** ——
所以它挂在 `Material` 上，只有那些"用材质画物体"的 Pass（现在只有 `BasePass`）才关心它。

> ★ 这也是为什么 `ShadowPass` / `GBufferPass` **不需要材质**：它们要的是"所有投影者/所有不透明物体，
> 用我自己的着色器和状态"。这就是 Pass 化的另一个收益 —— 材质只服务于真正需要它的那个 Pass。

### 9.3 渲染队列与排序

两个目标：**正确性**（半透明必须最后画）和**性能**（同 shader / 同状态连在一起画）。见 7.3 的四级 key。

**一个"活的回归测试"**：`main.cpp` 里三条命令的**提交顺序是故意打乱的**（半透明先提交），
所以只要排序坏了，画面立刻变错 —— 相当于一个不需要测试框架的回归测试。（建议在这几行旁边加注释说明是故意的。）

### 9.4 渲染状态管理（★ Pass 化之后改成了"无条件发"）

`glEnable` / `glDisable` 改的是**上下文的全局状态**，而且发出去就要花驱动的钱，
所以一开始的写法是"缓存上一次真正设过的状态，只在变化时才发"：

```cpp
if (renderState.depthTest != mRenderState.depthTest) {   // ← 老写法，现在 BasePass 里已经没有了
    if (renderState.depthTest) glEnable(GL_DEPTH_TEST);
    else                       glDisable(GL_DEPTH_TEST);
    mRenderState.depthTest = renderState.depthTest;   // ★ 必须同步缓存
}
```

> **铁律：这份缓存是 GL 真实状态的"镜像"。一旦不一致，之后所有 `!=` 比较都会失真。**

**而 Pass 化之后，这条铁律变得几乎不可能维持**：8 个 Pass 里有 7 个都要改深度状态，
如果每个 Pass 各存一份缓存，**它们对同一个 GL 全局状态的认知必然互相打架** ——
这个坑已经踩过两次（`Clear()` 的 depth mask、ShadowPass 的 depth mask）。

**所以现在 `BasePass` / `ShadowPass` / 各全屏 Pass 都选择"每次绘制前无条件把状态发出去"，不做缓存**
（`BasePass.cpp` 文件顶部的注释写得很明白）。代价是几次多余的 `glEnable`，在现在这个物体数量下完全无感。

**正解**（还没做，见 11.4 N15）：把状态缓存抽成一个**共享的** `OpenGLStateCache`，
由 renderer 持有、放进 `ctx` 让所有 Pass 共用 —— **缓存只有一份，才可能和 GL 真实状态保持一致**。
到那时才应该恢复"只在变化时才发"。

**`OpenGLRenderer::mRenderState` 现在只剩一个用途**：`Clear()` 里同步 `depthWrite`。

> **一个经典陷阱（已修复 ✅）**：`glClear(GL_DEPTH_BUFFER_BIT)` **受 `glDepthMask` 控制**！
> 上一帧若把 `depthWrite` 设成 `false`（透明地面就会），本帧的 `Clear()` 就**清不掉深度缓冲**。
> 现在 `Clear()` 开头会先 `glDepthMask(GL_TRUE)` 并同步缓存。
> **每个要清深度的 Pass 也一样**（`ShadowPass` 在清之前显式 `glDepthMask(GL_TRUE)`）。

### 9.5 矩阵归属

| 矩阵 | 由谁算 | 存在哪 | 怎么到 GPU |
|---|---|---|---|
| **Model** | `Transform`（位置/旋转/缩放） | `RenderCommand::transform` | `TransformToModelMatrix()` 现算（每个 Pass 各自算） |
| **View** | `Camera::Update()` | `CameraData::viewMatrix` → `ctx.viewMatrix` | 每帧随 `CameraData` 传给后端 |
| **Projection** | 同上，`aspect` 由 `SetViewportSize` 喂入 | `CameraData::projectionMatrix` → `ctx.projectionMatrix` | 同上 |
| **lightView / lightProjection** | ★ `ShadowPass::UpdateLightMatrices(ctx.lightPos)`，**每帧重算** | `ShadowPass` 的私有成员 | `SetMatrix(model, mLightView, mLightProjection)` |
| **lightSpaceMatrix** | 同上（= `P * V`） | `ctx.lightSpaceMatrix` | `SetMat4("lightSpaceMatrix", ...)`（`ScreenShadowPass` 用） |
| **InvProjection** | 各全屏 Pass 现算 `glm::inverse(ctx.projectionMatrix)` | — | `SetMat4("InvProjection", ...)`，用来从深度重建世界坐标 |

**三条原则**：

1. 相机**不主动去问窗口大小**（让逻辑层反向依赖平台层是错的）；窗口大小的真相源在渲染后端，由**上层**转发。
2. **着色用的光和投影用的光必须来自同一个来源**（6.7 的坑）——
   现在光源的唯一真相源是 `OpenGLRenderer::lightPos`，每帧填进 `ctx`，`ShadowPass` 从 ctx 读它算灯光矩阵。
3. **`lightSpaceMatrix` 只在 `ShadowPass` 里算一次**，通过 `ctx` 传给 `ScreenShadowPass`
   （原来是在每个物体上重复算 `lightProjection * lightView`，S7 已修）。

### 9.6 渲染循环顺序

```
① PollEvents()          ← 先处理输入（回调写 camera 状态；也处理按 G 切 SSGI）
② camera.Update()       ← 再更新逻辑（只改数据，不做任何 GL 调用）
③ Sort()                ← 排序（相机在动，必须每帧）
④ Clear()               ← 清屏（注意 depth mask 的坑）
⑤ ExecuteRenderCommands ← 内含 8 个 Pass（见 5.4）
⑥ SwapBuffers()         ← 呈现
```

### 9.7 透明物体

半透明物体要正确工作，需要"三件套"：
```cpp
renderState.depthTest  = true;                  // 测深度：让远处被前面的不透明物体挡住
renderState.depthWrite = false;                 // 不写深度：不要挡住后面画的东西
renderState.blend      = BlendMode::AlphaBlend; // 开混合
```
再加上**绘制顺序**（由 `RenderQueue` 保证）：透明物体必须排在所有不透明物体之后。

> **"测深度"和"写深度"是两个独立开关**。深度写入对应 `glDepthMask`，**不是** `glEnable(GL_DEPTH_TEST)`。

> ★ 另外要注意：**透明物体不进 G-Buffer**（`GBufferPass` 会跳过 `blend != Opaque` 的命令），
> 所以它也不会参与 AO / 屏幕空间阴影 / SSGI 的几何 —— 这是当前的权宜做法，见 S4。

---

## 十、Shader 与顶点格式约定

### 10.1 顶点属性（由 `OpenGLMesh` 设定，着色器必须一致）

```glsl
layout (location = 0) in vec3 aPos;      // 位置，偏移 0 字节
layout (location = 1) in vec3 aNormal;   // 法线，偏移 12 字节
layout (location = 2) in vec2 aTexCoor;  // UV，  偏移 24 字节
// 步长（stride）= 32 字节
```

### 10.2 Uniform 约定

**语义化 setter（`IShader::Set*`）**：

| uniform | 类型 | basic* | groundNet* | gbuffer* | 由谁设置 |
|---|---|---|---|---|---|
| `ModelMatrix` / `ViewMatrix` / `ProjectionMatrix` | mat4 | ✔ | ✔ | ✔ | `SetMatrix` |
| `mainLightPos` / `mainLightColor` | vec3 | ✔ | ✔ | ✖ | `SetLight` |
| `CameraPos` | vec3 | ✖ | ✔ | ✔ | `SetCamera` |

**通用 setter（`SetInt` / `SetMat4` / `SetVec2`，按名字传）**：

| uniform | 类型 | 用在哪个 Pass 的着色器 | 由谁设置 |
|---|---|---|---|
| `shadowMap` | sampler2D | `screen_shadow_frag` | `SetInt("shadowMap", 2)` |
| `lightSpaceMatrix` | mat4 | `screen_shadow_frag` | `SetMat4(...)` |
| `gNormal` / `gDepth` | sampler2D | `ssao_frag` / `screen_shadow_frag` / `ssgi_frag` / `ssgi_blur_frag` | `SetInt` |
| `ssaoInput` / `blurInput` | sampler2D | `ssao_blur_frag` / `screen_shadow_blur_frag` | `SetInt` |
| `ssgiInput` | sampler2D | `ssgi_blur_frag` | `SetInt("ssgiInput", 0)` |
| `baseColor` | vec3 | `basicfrag` | `SetVec3("baseColor", material->baseColor)`（`BasePass`） |
| `Projection` / `InvProjection` | mat4 | 各全屏 Pass | `SetMat4` |
| `screenShadow` | sampler2D | `basicfrag` / `ssgi_frag` | `SetInt` |
| `aoMap` / `ssgiMap` | sampler2D | `basicfrag` | `SetInt` |
| `screenSize` | vec2 | `basicfrag` | `SetVec2` |

> 对没有声明该 uniform 的 shader 调用无害：`glGetUniformLocation` 返回 -1，而 `glUniform*` 在 location 为 -1 时**被规范要求忽略**。

### 10.3 着色器清单（16 个文件 / 10 个 program）

| 着色器 | 用途 | 说明 |
|---|---|---|
| `basicvertex` / `basicfrag` | 猴头 + 三个平面 | 顶点输出 `vertexNormal` + `posWS`；片元只做 3 次屏幕空间采样（阴影/AO/SSGI）+ 乘 `baseColor`，**不再自己算 PCSS** |
| `groundNetVertex` / `groundNetFrag` | 半透明网格地面 | 用 `fwidth` 做屏幕空间抗锯齿的多层网格（小格 + 大格 + 红/蓝坐标轴）；按距离淡出；**★ 完全不采样任何屏幕空间纹理** |
| `shadow_mapping_depth_vert` / `_frag` | `ShadowPass` | 顶点只输出 `gl_Position`；**片元 `main(){}` 是空的**（只有深度有意义） |
| `gbuffer_vert` / `gbuffer_frag` | `GBufferPass` | 法线用**逆转置矩阵**变换；MRT 一次写 `gNormal`(loc 0) + `gDepth`(loc 1) |
| `fullscreen_vert` | **所有全屏 Pass 共用** | 不靠 VBO：顶点位置由 `gl_VertexID` 算出，3 个顶点盖满屏幕 |
| `ssao_frag` / `ssao_blur_frag` | `SSAOPass` / `SSAOBlurPass` | 32 核黄金角螺旋 + range/normal check + 距离淡出；5×5 深度加权模糊 |
| `screen_shadow_frag` / `screen_shadow_blur_frag` | `ScreenShadowPass` / `ScreenShadowBlurPass` | 屏幕空间 PCSS（blocker search + 可变半径 PCF）；双边模糊去噪 |
| `ssgi_frag` | `SSGIPass` | 单次弹射：24 射线 × 24 步屏幕空间步进 + 余弦加权 + 厚度上界 + 背面剔除 |
| `ssgi_blur_frag` | `SSGIBlurPass` | 9×9 深度 + 法线加权双边模糊（去 GI 噪点） |

> **地面（栅格）不参与屏幕空间效果是设计选择** —— 它是纯可视化用的辅助栅格（带坐标轴），
> 不接收光照/阴影/AO/SSGI，所以 `groundNetFrag` 里连 `aoMap` 都没声明。
> 能看见猴头影子的那块白色平板是 `plane2`，它复用了猴头的 `material`（所以会采样屏幕空间阴影）。

> **`fullscreen_vert` 为什么不需要 VBO**：3 个顶点里顶点 1/2 故意跑到屏幕外
> （`(2,0)` 和 `(0,2)`），三角形把 `[-1,1]²` 完全盖住，插值出来的 `vUV` 在屏幕内正好是 `[0,1]`。
> 比全屏四边形少一个顶点、少一条对角线边界，代价是要有一个空 VAO
> （Core Profile 规定"必须绑定一个 VAO 才能 draw"，哪怕它一个属性都没有）。

---

## 十一、已知问题与 TODO

### 11.1 Shadow Pass / Pass 管线

> **★ Pass 化一次性修掉了一大批老问题**（下面用 ✅ 标出）。老的编号保留，方便对照以前的记录。

| # | 状态 | 问题 | 现在的情况 |
|---|---|---|---|
| **S1** | ✅ **已修** | `~OpenGLRenderer` 不释放 `depthTex` / `shadowFBO` / `mShadowShader` | 这些成员**已经不存在了**。资源归各 Pass 所有，`Setup()` 建、析构删；`~OpenGLRenderer` 只做 `mPasses.clear()` → `glfwTerminate()` |
| **S2** | ✅ **已修** | 材质纹理从不给解绑 → 下一帧当附件写形成反馈循环 | 每个 Pass 收尾都显式 `glBindTexture(GL_TEXTURE_2D, 0)`（0/1/2 号单元逐个解） |
| **S3** | ✅ **已修** | `basicfrag` 里 `ShadowFactor(posWS)` 算了没用 | `basicfrag` 已重写，死代码随之消失 |
| **S4** | ⬜ **仍然存在** | 用 `blend != Opaque` 来筛"投影者" / "进 G-Buffer 的物体" | **现在有 2 处这样的判断**（`ShadowPass`、`GBufferPass`）。"是否透明"和"是否投影/是否进 G-Buffer"是三件事 |
| **S5** | ✅ **已修** | `1024` 硬编码在 2 处 | 现在是 `ShadowPass::kMapSize` 一个常量，`glTexImage2D` 和 `glViewport` 都用它 |
| **S6** | ✅ **已修** | 灯光矩阵算两遍 + `MatrixTools.h` 里两个死函数 | `lightProjection/lightView` 只剩 `ShadowPass` 里的 `mLightProjection/mLightView` 一份（每帧重算）；`MatrixTools.h` 里那两个 `GetLightSpace*` 已删除 |
| **S7** | ✅ **已修** | `lightProjection * lightView` 在每个物体上重复算 | 现在算一次存进 `ctx.lightSpaceMatrix`（见 `ShadowPass` 结尾） |
| **S8** | ⬜ **仍然存在** | `glEnable(GL_MULTISAMPLE)` 在 `ExecuteRenderCommands` 里每帧调用 | 代码里已经留了 `TODO(S8)`，应该挪到 `CreateWindow()` |
| **S9** | 🚧 **部分** | PCSS 参数硬编码 | `kPCSSBias` / `kPCSSSearchTexel` / `kPCSSLightTexel` / `kPCSSBlockerEps` 现在是 `screen_shadow_frag.glsl` 顶部的具名常量，**但仍然是编译期常量** —— 调参要改源码 + 重编，还没提成 uniform |
| ~~S10~~ | — | ~~`groundNetFrag.glsl` 不采样阴影图~~ | **按设计如此，不是缺陷**：地面是纯可视化用的栅格（含坐标轴），不参与光照/阴影 |
| **S11** | ✅ **已修** | `InitShadowPass()` 是 public | 该函数已删除（逻辑全在 `ShadowPass` 内部） |
| **S12** | ⬜ **仍然存在** | `VulkanRenderer.h` 只有声明、没有 `.cpp` | 切到 `VULKAN_RENDERER` 会链接失败。**注意：Pass 管线是 OpenGL 专属的，Vulkan 后端要重做一套**，所以这条比以前更"重"了 |
| **S13** | ✅ **已修** | 恢复视口用的是逻辑窗口尺寸 | 现在所有 Pass 都用 `ctx.fbWidth/fbHeight`（来自 `glfwGetFramebufferSize`）恢复视口 |

**Pass 化新引入/暴露的问题**：

| # | 问题 | 后果 | 建议 |
|---|---|---|---|
| **S14** | **3 个 Pass 的调试自检默认是开着的**：`SSAOPass::kDumpSSAO = true`、`ScreenShadowPass::kDumpScreenShadow = true`、`SSGIPass::kDumpSSGI = true` | 每次运行都会 `glReadPixels` **整张纹理**一次（`glReadPixels` 会强制 GPU 同步，很慢），并且往控制台打印几行统计 | 看够了改成 `false`（`GBufferPass::kDumpGBuffer` 已经是 `false`，照它改）；或者干脆收进一个宏，Release 下一律不编 |
| **S15** | `FullscreenQuad` 的 VAO 是**每个全屏 Pass 各自建一个**（6 个 Pass 各持有一个） | 6 个空 VAO —— 功能上无所谓，但本可以共用一个 | 抽成 renderer 级/静态的单例，或者放进 `ctx` 让全屏 Pass 共用 |
| **S16** | 每个 Pass 自己缓存了 `mWidth/mHeight`，而 `BasePass` 用的是 `ctx.fbWidth/fbHeight` | 在"窗口尺寸刚变、但本帧的 Pass 用的是上一帧尺寸"的边界上可能有一帧错配 | 统一从 `ctx` 取尺寸；或者把"帧尺寸"作为不可变快照，在帧开始时一次定好 |

### 11.2 渲染状态与绘制

| # | 问题 | 后果 | 建议 |
|---|---|---|---|
| **R1** | `RenderStateToOpenGL(BlendMode)` 返回单个值，却用来喂 `glBlendFunc` 的两个参数（现在在 `BasePass.cpp` 的匿名命名空间里） | `AlphaBlend` 恰好正确；**`Additive` / `Multiply` 是错的** | 改成 `switch`，一个 case 里同时设 `glBlendFunc(src, dst)` |
| **R2** | `EnableRendererFeature` / `DisableRendererFeature` 直接 `glEnable`/`glDisable` | 以前会**绕过状态缓存**导致缓存失真。★ Pass 化之后各 Pass 本来就"无条件发状态"，所以危害小了很多；但 `OpenGLRenderer::mRenderState` 会被它带偏（`Clear()` 还读 `depthWrite`） | 这两个接口目前**全项目没人调用**（`main` 里没调）。要么删掉，要么让它同步更新 `mRenderState` |
| **R5** | 每次 `Set*` 都 `glGetUniformLocation` | 每帧几百次无谓的字符串查找。★ Pass 化之后调用次数是原来的好几倍 | 链接成功后查一次并缓存 location |
| **R6** | `basicfrag` / `ssgi_frag` 里 `normalize(mainLightPos)` 把**灯的位置**当**方向**用（`SetLight` 传进去的是 `lightPos`） | 物体离原点越远，"指向光源的方向"错得越厉害 —— 现在三个物体都在原点附近所以看不出来 | `SetLight` 改传真正的**光线方向**（平行光＝固定方向；点光＝`normalize(lightPos - worldPos)`），或把 uniform 改名成 `lightDir`。**注意 SSGI 里也有同一处假设**（`ssgi_frag.glsl` 的 `L = normalize(mainLightPos)`），要一起改 |

> ✅ **已修复**：`Clear()` 的深度 mask 问题；渲染队列没有排序（现在有 `RenderQueue::Sort`）；S2 的纹理解绑（现在每个 Pass 都解绑）。

### 11.3 摄像机与输入

| # | 状态 | 问题 | 建议 |
|---|---|---|---|
| **C1** | ⬜ | `Camera.cpp` 里 `if (dragging)` 被注释掉 | 恢复它，否则"不按左键光移动鼠标"也会旋转 |
| **C2** | ⬜ | `main.cpp` 里 `camera.mouseX += 1.0` | 删掉（让相机自转的调试代码） |
| **C3** | ⬜ | 没有任何鼠标回调注册 | `glfwSetCursorPosCallback` / `glfwSetMouseButtonCallback` → 转发给 `Camera::BeginDrag/EndDrag`。**后端只暴露"回调注册"接口，由上层转发**，不要让 `OpenGLRenderer` 认识 `Camera` |
| **C4** | ✅ **基本已修** | 没有处理窗口 resize | 现在**不需要 GLFW 回调**：`main` 每帧 `camera.SetViewportSize(renderer->GetWindowSize())`，`ExecuteRenderCommands` 每帧比较尺寸并调 `OnResize`，各 Pass 自己重建 RT。剩下的是注册 `glfwSetFramebufferSizeCallback` 会更省（避免"窗口变了但这一帧还没同步"的一帧错配） |
| **C5** | ✅ **已修** | `GetWindowSize()` 返回构造时的固定值 | 现在实时 `glfwGetFramebufferSize`（并在 `window == nullptr` 时保护返回 0） |
| **C6** | 🚧 | `aspectRatio` public / `viewportSize` private，两者可能不同步 | 统一成 private + 单一入口（`main` 现在其实用的是 `camera.aspectRatio` 去算正交投影，所以它暂时还不能私有化） |
| **C7** | ⬜ **新** | 窗口最小化时 `glfwGetFramebufferSize` 会返回 `0 × 0` | `Camera::SetViewportSize` 有防除零（`aspect = 1.0`），各 Pass 的 `OnResize` 也有 `if (w <= 0) return;` —— 但投影矩阵会退化成 `aspect = 1`。更稳的做法是在尺寸为 0 时跳过整帧渲染 |

### 11.4 架构与解耦

| # | 问题 | 建议 |
|---|---|---|
| **N5** | `IShader::GetID()` 把"program id"这个 OpenGL 概念放进抽象接口，且没人用 | 删掉，或改成不暴露底层句柄的语义 |
| **N6** | `OpenGLShader::BuildFromFiles` 没写 `override` | 加上（签名写错时能编译期发现） |
| **N7** | `OpenGLShader` 的 `const mVertexPath / mFragmentPath` 已无人使用 | 删掉 |
| **N8** | `main` 丢弃 `BuildFromFiles` 的返回值 | shader 加载失败时静默继续，之后画面全黑很难查 → 检查返回值 |
| **N9** | `RenderQueue`：`key` 重复算 `O(n log n)` 次；手写构造/析构多余且**抑制移动语义** | 预计算 key；删掉手写的构造/析构（零规则） |
| **N10** | 🚧 **部分已修**：两个 `GetLightSpace*` 死函数已删除；但 `Transform` 还住在 `MatrixTools.h` 里 | 把 `Transform` 拆到 `Core/Transform.h`，`MatrixTools.h` 名副其实 |
| **N11** | `Material` 构造缺 `explicit` | 加上 |
| **N12** | `IRenderer` 接口偏胖（窗口 + 资源工厂 + 渲染命令混在一起） | 早晚拆成 `IDevice`（资源工厂）/ `IRenderer`（帧） |
| **N13** | `IRenderer::GetWindow()` 返回 `void*` | 需要时引入前向声明的句柄类型 |
| **N14** | 后端清单在 `RendererFactory.h` 和 `.cpp` 里各一份 | 加后端要改两处；写注释互相提醒 |
| **N15** | ⬜ **新**：Pass 之间的**渲染状态没有共享的缓存**（各 Pass 无条件发状态，`OpenGLRenderer::mRenderState` 半废弃） | 抽一个 `OpenGLStateCache` 放进 `ctx`，由所有 Pass 共用 —— **缓存只有一份才可能和 GL 真实状态一致**，那时再恢复"只在变化时才发"（见 9.4） |
| **N16** | ⬜ **新**：每个 Pass 各自持有一个 `std::unique_ptr<OpenGLShader>`，各自 `BuildFromFiles` | 现在有 6 个全屏 Pass 各自编译一个 program。功能上没问题，但 `SSAOBlur` / `ScreenShadowBlur` **两个片元着色器几乎一模一样**（只差输入名和权重数量），`SSGIBlur` 也只多了一个法线权重 —— 可以合并成"一个带权重的模糊 program"，用 uniform/宏区分 |
| **N17** | ⬜ **新**：`OpenGLShader::compileShader` 编译失败时只打印日志、**仍然返回 shader 对象** | 接着照样 attach + link（链接会失败，所以最终能靠 `GL_LINK_STATUS` 兜住），但错误信息会绕一圈、难定位 → 编译失败时返回 0 并让 `BuildFromFiles` 立刻 `return false` |

> ✅ **已修复**：`Shader`/`Mesh` 直接调 `gl*` 且放在 `src` 根目录（已拆成 `IShader`/`IMesh` + `Render/OpenGL/*`）；
> `Material` 间接依赖 glad；`IRenderer::Render(Mesh*,Shader*,mat4&)` 空实现（已删）；
> `config.h` 万能头（已瘦身 + 加 `#pragma once`）；`RendererFactory` 的 `return nullptr`（已改 `#error`）；
> `RendererFactory.h` 对 `config.h` 的隐式依赖（已显式 include）；`MatrixTools.h` 缺 `inline`（已加）。

### 11.5 资源生命周期与内存

| # | 问题 | 后果 | 建议 |
|---|---|---|---|
| **M1** | `main` 结尾**没有 `delete plane3`** | 泄漏（`plane2` 已经补上了，`plane3` 是后来加的，忘了跟着删） | 补上 `delete plane3;` |
| **M2** | `plane2->SetData(objMeshData1)` / `plane3->SetData(objMeshData1)` 用的是**第一个平面**的数据（应该是 `objMeshData2` / 重新加载） | 三个平面内容相同所以现在看不出问题，但 `objMeshData2` 白加载了 | 改成各自的数据；或干脆只加载一份共用 |
| **M3** | `main` 提前 `return -1` 的路径不释放已创建的资源 | 泄漏（进程即将退出，影响小） | 收进一个 `Application` 类 |
| **M4** | `OpenGLMesh::SetData` 重复调用会 `glGen*` 并覆盖旧句柄 | 旧的 VAO/VBO/EBO 泄漏 | 先删旧的，或加"只允许设置一次"的断言 |
| **M5** | `IMesh::SetData(const float*, int vertexCount, ...)` 里的 `vertexCount` 语义其实是"**float 个数**"（`OpenGLMesh` 里拿它 × `sizeof(float)` 传给 `glBufferData`），名字有歧义 —— 真正与之对应的 `ObjMeshData::VertexCount()` 返回的却是**顶点数** | 两个名字相同、语义差 8 倍，很容易传错；而且现在两者同时存在 | 改名成 `floatCount`，或统一改成"顶点数 × 8" |

### 11.6 清理项

- `main.cpp`：`vertices` / `indices` / 局部 `view` / `projection` / `aspect` / `OrthoProjectionMatrix` / `viewWidth` / `viewHeight` 全是死代码
- `main.cpp`：`//#include "OpenGLRenderer.h"` 和 `//IRenderer* renderer = new OpenGLRenderer(...)` 两条死注释
- `OpenGLMesh.h` / `OpenGLMesh.cpp`：注释里的"每个顶点 6 个 float / stride 24"已过时（实际 8 个 / 32）
- `OpenGLRenderer.h`：`window` 是 public，建议改 private
- `OpenGLRenderer.h`：`lightPos` 注释写着"渲染器暂时替场景保管"，实际已经是唯一真相源；将来要抽成 `Scene`/`FrameData`
- **3 个 Pass 的调试自检默认是开的**（`kDumpSSAO` / `kDumpScreenShadow` / `kDumpSSGI`）→ 改成 `false`（S14）
- `Resources/ResourceManager.h`：空文件
- `目标渲染架构.md`：内容已严重过时（见 7.6）
- `mesh/*.mtl`：文件里只有注释、没有材质定义，所以模型用默认材质
- `.commandcode/`：工具配置目录（不属于本项目代码，也不是 git 忽略项）

### 11.7 下一步（按优先级）

1. **关掉调试自检**（S14）—— 三行改动，直接拿回一大截帧率：`kDumpSSAO` / `kDumpScreenShadow` / `kDumpSSGI` 改成 `false`
2. **抽共享的 `OpenGLStateCache`**（N15 / 9.4）—— 放进 `ctx`，让所有 Pass 共用。
   这是把"无条件发状态"换回"只在变化时才发"的**前提**，也是唯一能让缓存与 GL 真实状态保持一致的形态
3. **理顺"光照方向"的语义**（R6）—— `mainLightPos` 是灯的位置却被当方向用，
   现在有三个着色器依赖这个（错误的）假设。**改的时候三处要一起改**，否则又会出现"着色的光和投影的光不一致"那类 bug
4. **修 RenderQueue / `Set*` 的热点**（N9 / R5）—— `RenderQueue` 预计算 key（现在每帧多算了 `O(n log n)` 次 `dot`）；
   `OpenGLShader` 缓存 uniform location（Pass 化之后每帧 `Set*` 次数是原来的好几倍）
5. **`Material::castShadow` / `receivesShadow` / `inGBuffer`**（S4）—— 现在有 2 处靠 `blend != Opaque` 猜意图
6. **修摄像机输入通路**（C1 / C2 / C3）—— 恢复 `if(dragging)`、删掉自转、注册回调
7. **`RT` 的绑定/解绑收口** —— 引入 `IRenderTarget`（`Bind()` / `Unbind()` 内部管 FBO + 视口），
   顺便解决 S16（尺寸来源不一）、S15（6 个重复的空 VAO）、以及"每个全屏 Pass 都要抄一遍收尾四行"
8. **合并三个模糊 Pass**（N16）—— `SSAOBlur` / `ScreenShadowBlur` / `SSGIBlur` 是同一个算法的三种权重组合，
   值得抽成一个可配的"模糊 Pass"
9. **再往后** —— `FrameData`（把光源/清屏色也变成"每帧传一次"）、`Scene` 层、`ResourceManager`、
   **把 `目标渲染架构.md` 更新到与 Pass 管线一致**、`PostProcess` Pass（色调映射/泛光）、SSRT（屏幕空间反射）、
   以及 S12 提到的 Vulkan 后端（注意：Pass 管线本身是 OpenGL 专属的，那边要重做一套）

---

## 十二、变更记录

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

### 第三轮：Pass 化 + 屏幕空间效果（★ 当前）

| 项目 | 之前 | 现在 |
|---|---|---|
| **渲染管线** | `ExecuteRenderCommands` 里硬编码 ShadowPass + BasePass；中间 RT 全挂在 `OpenGLRenderer` 上 | `vector<unique_ptr<OpenGLRenderPass>>` + `Stage()` 排序；`ExecuteRenderCommands` 只剩"填 ctx → OnResize → 跑一遍" |
| **Pass 之间怎么通信** | 直接读 Renderer 的私有成员 | 每帧一个 `OpenGLRenderContext`，Pass 只认识它 |
| **中间 RT 的所有权** | `OpenGLRenderer` 持有、**手写释放（且当时没写）** | 每个 Pass 自己持有、RAII（`Setup()` 建 / 析构删） |
| **`OpenGLRenderer.h`** | `mShadowShader` / `depthTex` / `shadowFBO` / `lightProjection` / `lightView` / `ApplyRenderState` / `RenderStateToOpenGL` / `InitShadowPass()` | **上面全部删除**，一个 GPU 句柄都不持有 |
| **新增 Pass** | Shadow + Base（2 个） | `GBuffer` → `SSAO` → `SSAOBlur` → `ScreenShadow` → `ScreenShadowBlur` → `SSGI` → `SSGIBlur`（共 **8 个**） |
| **G-Buffer** | 无 | 一个 FBO + MRT：世界法线（RGBA16F）+ 世界深度（R32F，到相机的距离）+ 深度 RBO |
| **SSAO** | 无 | 32 点黄金角螺旋 + `range check` + `normal check` + **距离淡出**；另起一趟 5×5 **深度加权**模糊 |
| **阴影** | `basicfrag` 里每个材质像素跑 32 次采样的 PCF | **搬到屏幕空间**：`ScreenShadowPass`（半分辨率 PCSS：blocker search + 可变半径 PCF）→ `ScreenShadowBlurPass`（双边模糊）→ 材质只采样 1 次 |
| **SSGI** | 无 | 半分辨率单次弹射：24 射线 × 24 步屏幕空间步进 + 余弦加权 + 厚度上界 + 背面剔除 + 起点抬起；再接 **`SSGIBlurPass`**（9×9 深度 + 法线加权双边模糊）去噪。**可运行时按 G 开关**，关掉时纹理清 0 → 与"没有 SSGI"逐像素一致 |
| **`basicfrag.glsl`** | ~70 行 PCSS | **只剩 3 次纹理采样**（阴影 / AO / 间接光），最后乘 `baseColor` |
| **材质颜色** | 无（着色器写死白） | `Material::baseColor` + `IShader::SetVec3`；三个平面共用同一个 shader 程序、只换参数 |
| **`IShader`** | `SetMat4` / `SetInt` | 再加 `SetVec2`（屏幕空间 UV 需要"屏幕尺寸"当分母） |
| **新着色器** | 6 个 | **16 个**：`fullscreen_vert`、`gbuffer_*`、`ssao_frag`、`ssao_blur_frag`、`screen_shadow_frag`、`screen_shadow_blur_frag`、`ssgi_frag`、`ssgi_blur_frag` |
| **`GetWindowSize()`** | 返回构造时的固定值（C5） | 实时 `glfwGetFramebufferSize` |
| **resize** | 不处理（C4） | 每帧比较尺寸 → `OnResize` → 各 Pass 重建自己的屏幕尺寸 RT；视口一律用 `ctx.fbWidth/fbHeight`（C5 / S13 一起修） |
| **一次性修掉的老问题** | — | S1（资源不释放）、S2（纹理解绑/反馈循环）、S3（死代码）、S5（`1024` 硬编码）、S6（灯光矩阵算两遍 + 死函数）、S7（每物体重算 `P*V`）、S11（`InitShadowPass` public）、C4、C5、S13 |
| **新引入的问题** | — | S14（调试自检默认开着，含 `glReadPixels`）、S15（6 个重复的空 VAO）、S16（各 Pass 自缓存尺寸）、N15（没有共享状态缓存）、N16（三个模糊着色器重复）、N17（编译失败仍返回 shader） |

### 现在的架构一句话总结

> **接口在中间，实现在下层，装配只有一处；渲染管线是一串按 `Stage()` 排序的 Pass，
> Pass 之间只通过每帧的 `OpenGLRenderContext` 交换数据 —— 加一个效果只需要动 `Passes/` 里的文件。**

**三条验收标准**：
1. `main.cpp` 里搜不到 `OpenGL` / `glad` / `glfw`（除了两条被注释掉的遗留写法）。
2. `OpenGLRenderer` 里搜不到任何 GPU 句柄（`unsigned int` 纹理/FBO）—— 中间 RT 全部归 Pass 所有。
3. 想加一个后处理，`ExecuteRenderCommands` **一个字都不用改**。

### 还没做完的

见 [11.1](#111-shadow-pass--pass-管线) ~ [11.7](#117-下一步按优先级)。

眼下最值得做的三件：
1. **关掉三个调试自检**（S14）—— 三行改动，直接拿回一大截帧率；
2. **抽共享的 `OpenGLStateCache`**（N15）—— 它是"只在变化时才发状态"的前提；
3. **理顺光照方向语义**（R6）—— 现在有 3 个着色器依赖同一个错误假设，改的时候必须一起改。
