# OpenGLRenderer

一个从零手写的 OpenGL 渲染引擎（学习项目），目标是把"散落在 main 里的 OpenGL 调用"逐步重构成一套分层的渲染架构。

**技术栈**：C++17 · OpenGL 4.6 Core Profile · GLFW · GLAD · GLM · tinyobjloader

**当前能跑出来的东西**：一个可旋转的轨道摄像机视角下，一个猴头模型（不透明）+ 一片半透明的地面网格。

**当前架构状态**：已完成"抽象接口 + 具体实现 + 工厂"的第一轮解耦。应用层（`main.cpp`）里**不出现任何 OpenGL 头文件**，后端由编译期宏选择、由唯一的工厂文件装配，`OpenGLRenderer.h` 与 `VulkanRenderer.h` 互不认识。

---

## 目录

- [一、构建与运行](#一构建与运行)
- [二、目录结构](#二目录结构)
- [三、分层与依赖规则](#三分层与依赖规则)
- [四、后端选择机制（宏 + 工厂）](#四后端选择机制宏--工厂)
- [五、文件清单](#五文件清单)
- [六、文件之间的关系](#六文件之间的关系)
- [七、关键概念](#七关键概念)
- [八、Shader 与顶点格式约定](#八shader-与顶点格式约定)
- [九、已知问题与 TODO](#九已知问题与-todo)
- [十、本轮重构变更记录](#十本轮重构变更记录)

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
| GLM | 纯头文件 | 数学（vec3 / mat4 / perspective / lookAt） |
| tinyobjloader | 纯头文件 | 解析 `.obj` 模型 |

### 构建

```bash
cmake -S . -B build
cmake --build build
```

### 运行

```bash
build/OpenGLRenderer.exe
```

> 程序用编译期宏 `PROJECT_SOURCE_DIR`（由 CMake 通过 `target_compile_definitions` 注入）
> 拼出 shader / obj 文件的**绝对路径**，所以从哪个目录启动都能找到资源。

### 改代码时的四个坑

1. **新增 `.cpp` 必须写进 `CMakeLists.txt` 的 `add_executable`**，否则它不会被编译，调用处会在**链接阶段**报 `undefined reference`。
2. **新增子目录必须加进 `target_include_directories`**，否则该目录下 `#include "xxx.h"` 会报 `No such file or directory`。
3. **改完 `CMakeLists.txt` 要重新跑一次 `cmake -S . -B build`**（改 CMake 脚本不会自动重配置）。
4. **切换渲染后端要改 `src/config.h` 里的宏**（见 [第四节](#四后端选择机制宏--工厂)），改完同样要重新编译。

---

## 二、目录结构

> 图例：✅ 已实现 · 🚧 有雏形但需要改 · ⬜ 待做（空文件或占位）

```
OpenGLRenderer/
├── CMakeLists.txt                    ✅ 构建脚本（源文件清单 + include 路径 + 第三方库）
├── README.md                         ✅ 本文件
├── .gitignore                        ✅
│
├── dependencies/                     ✅ 第三方库（随仓库分发）
│   ├── glad/                         GLAD（include + src/glad.c）
│   ├── glfw/                         GLFW（include + lib-mingw-w64 / lib-vc20xx）
│   ├── glm/                          GLM（header-only）
│   └── tinyobjloader/                tinyobjloader（header-only）
│
└── src/
    ├── main.cpp                      ✅ 程序入口 + 主循环（已不含任何 OpenGL 头）
    ├── config.h                      🚧 只剩一行后端选择宏（不再是"万能头"）
    │
    ├── Render/                       🚧 渲染核心层（与图形 API 无关）
    │   ├── Renderer.h                ✅ IRenderer 抽象接口 + CreateRenderer 声明
    │   ├── RendererFactory.h         ✅ 工厂私有头：按宏 include 对应后端
    │   ├── RendererFactory.cpp       ✅ 全项目唯一的"装配点"
    │   ├── RenderCommand.h           ✅ Transform + RenderCommand
    │   ├── Material.h / .cpp         ✅ 材质：IShader + RenderState
    │   ├── IShader.h                 ✅ 着色器抽象接口
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
    │   └── plane.obj  / plane.mtl    ✅ 单位平面（4 顶点 2 三角形）
    │
    ├── shaders/
    │   ├── basicvertex.glsl          ✅ 猴头用的顶点着色器
    │   ├── basicfrag.glsl            ✅ 猴头用的片段着色器（半兰伯特光照）
    │   ├── groundNetVertex.glsl      ✅ 地面网格用的顶点着色器
    │   └── groundNetFrag.glsl        ✅ 地面网格用的片段着色器（fwidth 抗锯齿网格）
    │
    ├── 目标渲染架构.md                ✅ 架构规划文档
    └── Core/
        └── 想法.md                   ✅ 随手记的疑问（沟通用）
```

**不属于本项目的目录**：`build/`（CMake 生成物，已在 `.gitignore` 里）。

**已删除的旧文件**（本轮重构）：`src/Shader.h/.cpp`、`src/Mesh.h/.cpp`、`src/RenderObject.h/.cpp`。
它们被 `Render/IShader.h` + `Render/OpenGL/OpenGLShader.*`、`Render/IMesh.h` + `Render/OpenGL/OpenGLMesh.*` 取代。

---

## 三、分层与依赖规则

### 3.1 分层

```
        main.cpp                      ← 应用层：主循环、装配调用、收集渲染命令
            │  只认识 IRenderer / IMesh / IShader / Material / Camera
            │
   ┌────────┴─────────────────────────────────────────┐
   │ Render/           渲染核心层（与图形 API 无关）      │
   │   Renderer.h        IRenderer     后端抽象接口      │
   │   IShader.h         IShader       着色器抽象接口    │
   │   IMesh.h           IMesh         网格抽象接口      │
   │   RenderCommand.h   一条渲染命令                   │
   │   Material.h        材质 = IShader + 渲染状态       │
   │   Camera/           摄像机，产出 View / Projection  │
   └────────┬─────────────────────────────────────────┘
            │  （接口层只被"实现"与"工厂"依赖）
   ┌────────┴─────────────────────────────────────────┐
   │ Render/OpenGL/    具体实现（唯一出现 gl* / glfw*）  │
   │   OpenGLRenderer : IRenderer                      │
   │   OpenGLShader   : IShader                        │
   │   OpenGLMesh     : IMesh                          │
   └──────────────────────────────────────────────────┘

   Render/RendererFactory.cpp   ← 唯一的装配点：知道所有后端，负责 new 出具体实现
```

### 3.2 四条硬规则

| 规则 | 说明 | 现状 |
|---|---|---|
| ① **只有 `Render/OpenGL/` 里可以出现 `gl*` / `glfw*`** | 其它层一律不碰图形 API | ✅ 已达成 |
| ② **Camera 不知道窗口，也不知道渲染后端** | `aspect` 由上层喂进来（`Camera::SetViewportSize`） | ✅ 已达成 |
| ③ **Renderer 不知道 Camera 之外的场景数据** | 它只接收「一份相机数据 + 一串渲染命令」 | ✅ 已达成 |
| ④ **后端之间互不认识** | `OpenGLRenderer.h` 永不 include `VulkanRenderer.h`，反之亦然 | ✅ 已达成 |

> **判断一个设计对不对，问一句**：*"如果明天换成 DirectX 后端，哪些文件要改？"*
> 正确答案是：**只改 `Render/OpenGL/` 那一层 + `Render/RendererFactory.cpp` 的装配分支**。
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

- **所有实现指向接口**（实现依赖抽象）
- **main 指向接口**
- **只有工厂指向所有实现**

这就是"依赖倒置"：不是没有依赖，而是把"谁依赖谁"的方向掰直了，并且把"知道具体类型"这件事**关进一个笼子**（工厂文件）。

---

## 四、后端选择机制（宏 + 工厂）

这一节是本轮重构的核心，也是踩过坑的地方，**建议完整读一遍**。

### 4.1 三个角色的分工

| 文件 | 职责 | 认识谁 |
|---|---|---|
| `Render/Renderer.h` | 声明抽象接口 + `CreateRenderer` 的**声明** | 只认识 `IMesh` / `IShader` / `Camera` |
| `Render/RendererFactory.h` | 工厂的私有头：按宏 include 对应后端的头 | 有条件地认识所有后端 |
| `Render/RendererFactory.cpp` | **唯一**决定"创建谁"的 `.cpp` | 有条件地认识所有后端 |
| `config.h` | 一行 `#define`，选择后端 | — |
| `main.cpp` | 调用 `CreateRenderer()`，用 `IRenderer*` | 只认识 `IRenderer` |

### 4.2 为什么工厂是"自由函数"，不是成员函数

```cpp
// Renderer.h —— 声明写在 class 外面（namespace 作用域）
class IRenderer { ... };

IRenderer* CreateRenderer(int width, int height);
```

**不能**写成 `IRenderer` 的虚成员函数，原因有两个：

1. **名字可见性**：成员函数的名字不是裸名字，必须挂着对象用（`obj->CreateRenderer()`）。而 main 里第一时间恰恰还没有对象，裸调 `CreateRenderer()` 会报
   `error: 'CreateRenderer' was not declared in this scope`。
2. **逻辑上做不到**：虚函数调用要走虚表，**必须有对象实例才能查表**。而"创建第一个 renderer"这件事本身还没有对象，鸡生蛋。所以能打破这个循环的只能是**自由函数**或**static 成员函数**。

> 对照：`CreateShader()` / `CreateMesh()` 写成成员函数是**对的**——调用时 renderer 已经存在，有"前置对象"。
> 区别就在于**有没有前置对象**。

### 4.3 声明与定义分离（为什么 main 能用它）

编译产物里的符号（`nm` 可见）：

```
main.cpp.obj              U CreateRenderer(int, int)     ← U = Undefined，只有名字（欠条）
RendererFactory.cpp.obj   T CreateRenderer(int, int)     ← T = 已定义，有身体（实物）
```

- `Renderer.h` 里的 `IRenderer* CreateRenderer(int, int);` 是**声明**——main include 它就拿到了这句声明，编译能过。
- `RendererFactory.cpp` 里的实现是**定义**——提供实物。
- **链接器**把欠条和实物对上。所以 main 能调用它，哪怕 main 从来没听说过 `OpenGLRenderer` 这个类。

### 4.4 ⚠️ 宏的作用范围 = 一次编译单元（踩过两次的坑）

**`#define` 只在一个 `.cpp` 的编译单元里有效，不跨文件。**

- 每个 `.cpp` 是**一间独立的房间**，`#define` 是贴在这间房间墙上的便条，隔壁房间看不见。
- 编译器每次只看见"一个 `.cpp` + 它 `#include` 进来的头文件"，编完就清空记忆。
- 宏想跨文件生效，只有两条路：**① 写在一个被大家共同 include 的头里；② 由构建系统在命令行上加 `-D`**。

**坑 1（已经踩过）**：把 `#define OPENGL_RENDERER` 写在 `main.cpp` 里，然后在 `OpenGLRenderer.cpp` 里用 `#if defined(OPENGL_RENDERER)`。
→ 编 `OpenGLRenderer.cpp` 时宏不存在 → `#if` 判假 → 整个分支被预处理器删掉 → 函数返回 `nullptr` → main 调 `renderer->Init()` 时读地址 0 → **`0xC0000005` 访问冲突，窗口都出不来**。

**坑 2（当前的脆弱点）**：`RendererFactory.h` 自己**没有** `#include "config.h"`，它能工作纯粹是因为第 5 行先 `#include "Renderer.h"`，而 `Renderer.h` include 了 `config.h`。
→ 一旦把 `config.h` 从 `Renderer.h` 里摘掉（这是更干净的做法），这里的 `#if` 立刻判假 → 一个后端头都不 include → 编译报 `'OpenGLRenderer' was not declared`。
→ **修法**：在 `RendererFactory.h` 的 `#if` **之前**补 `#include "config.h"`（谁用谁 include）。

**自检技巧**：想知道某个宏在某个编译单元里是否可见，可以在文件顶部临时加：

```cpp
#if !defined(OPENGL_RENDERER)
#error "OPENGL_RENDERER not visible in this translation unit"
#endif
```

这样构建期就报错，比运行期 `0xC0000005` 早得多也清楚得多。

### 4.5 当前选择流程（一张图）

```
编译 RendererFactory.cpp
    │
    ├─ #include "RendererFactory.h"
    │       ├─ #include "Renderer.h"  →  #include "config.h"  →  #define OPENGL_RENDERER
    │       └─ #if defined(OPENGL_RENDERER)
    │              #include "OpenGL/OpenGLRenderer.h"     ← 只把这一个后端的头拉进来
    │          #elif defined(VULKAN_RENDERER)
    │              #include "Vulkan/VulkanRenderer.h"
    │          #endif
    │
    └─ 函数体：
        IRenderer* CreateRenderer(int width, int height) {
        #if defined(OPENGL_RENDERER)
            return new OpenGLRenderer(width, height);      ← 真正编进 exe 的那一行
        #elif defined(VULKAN_RENDERER)
            return new VulkanRenderer(width, height);
        #else
            return nullptr;                                ← ⚠️ 见 9.3 的 N4
        #endif
        }
```

**注意**：`#include` 也放在 `#if` 里面，所以工厂虽然"认识所有后端"，但**每次编译只把当前那个后端的头拉进来**，另一个后端的头根本不会被读到。这是"有条件地认识"的实现方式。

### 4.6 加一个 Vulkan 后端要做什么

1. 实现 `Render/Vulkan/VulkanRenderer.cpp`（+ `VulkanShader` / `VulkanMesh`）。
2. 在 `CMakeLists.txt` 里加上新 `.cpp`，并把 `src/Render/Vulkan` 加进 `target_include_directories`。
3. 把 `config.h` 里的宏换成 `#define VULKAN_RENDERER`。
4. `RendererFactory.h` / `RendererFactory.cpp` 里的 `#elif defined(VULKAN_RENDERER)` 分支**已经写好了**，不用改。
5. `main.cpp` **一个字都不用动**。

> ⚠️ 后端清单在 `RendererFactory.h` 和 `RendererFactory.cpp` **两个文件里各有一份**，加后端时两处都要同步（见 9.3 的 N11）。
>
> ⚠️ 后端到 3~4 个、或需要运行时切换时，`#if` 会开始爆炸。那时候再上"运行期注册表"或 dll 插件。

### 4.7 一个更极致的自检（可选加固）

目前 `main.cpp` 之所以能"偷偷"用到 glad，是因为 CMake 把 `src/Render/OpenGL` 的 include 路径给了整个 target（而且 main 不再 include 后端头了，所以现在是干净的）。
把工程拆成两个 target（接口层静态库 + 可执行文件），只给实现层的 target 加 OpenGL 的 include 路径，以后 main 里但凡漏 include 一个 OpenGL 头，**编译立刻失败**。不急着做。

---

## 五、文件清单

### 5.1 入口与配置

---

#### `src/main.cpp` ✅

**职责**：程序入口 + 渲染主循环。目前仍承担了较多职责（将来会被 `Application` / `Scene` 层接管）。

**关键点**：**已经不含任何 OpenGL 头文件**（`OpenGLRenderer.h` 的 include 被注释掉了）。它只 include `<iostream>` / `Camera.h` / `Renderer.h`。

**流程**：

```
1. IRenderer* renderer = CreateRenderer(800, 600);   // 工厂——具体后端由编译期宏决定
   renderer->Init()                                   // 建窗口 + 上下文 + 加载 GLAD

2. Camera camera;
   camera.SetViewportSize(renderer->GetWindowSize());

3. IShader* shader = renderer->CreateShader();        // 猴头：不透明
   shader->BuildFromFiles(vs, fs);
   Material material(shader);
   material.renderState.depthTest = true;

   IShader* planeShader = renderer->CreateShader();   // 地面：半透明
   planeShader->BuildFromFiles(vs2, fs2);
   Material planeMaterial(planeShader);
   planeMaterial.renderState.blend      = BlendMode::AlphaBlend;
   planeMaterial.renderState.depthTest  = true;    // 要测深度
   planeMaterial.renderState.depthWrite = false;   // 但不写深度

4. IMesh* mesh  = renderer->CreateMesh(); mesh->SetData(objMeshData);    // 上传到 GPU
   IMesh* plane = renderer->CreateMesh(); plane->SetData(objMeshData1);

5. 组装渲染队列：
   std::vector<RenderCommand> renderQueue;
   renderQueue.push_back(RenderCommand(mesh,  &material,      Transform(...)));
   renderQueue.push_back(RenderCommand(plane, &planeMaterial, Transform(...)));

6. 主循环：
   while (!renderer->WindowShouldClose()) {
       renderer->PollEvents();                        // 处理输入
       camera.mouseX += 1.0;                          // ← 调试用：驱动相机自转
       camera.Update();                               // 算 View / Projection
       renderer->Clear();                             // 清颜色 + 深度
       renderer->ExecuteRenderCommands(renderQueue, camera.GetCameraData());
       renderer->SwapBuffers();                       // 呈现
   }

7. delete mesh; delete plane;
   delete shader; delete planeShader;
   delete renderer;         // 顺序重要：GL 资源必须先于 renderer 销毁（见 6.2）
```

**依赖**：`Renderer.h`、`Camera.h`、`RenderCommand.h`（经 Renderer.h）、`Material.h`、`ObjLoader.h`、`<iostream>`
**被谁使用**：无（程序入口）

**已知问题**：
- `CreateRenderer(800, 600)` 用的是**硬编码数字**，上面已经有 `WINDOW_WIDTH/WINDOW_HEIGHT` 常量
- `camera.mouseX += 1.0;` 是调试代码，导致相机每帧持续自转
- `shader->BuildFromFiles(...)` 的**返回值被丢弃** → shader 加载失败时静默继续（见 N8）
- 大量死代码：`vertices` / `indices`（旧三角形数据）、局部 `view` / `projection` / `aspect` / `OrthoProjectionMatrix` 都不再被使用
- 两条 `LoadObj` 失败提前 `return -1` 的路径上，已 new 的 shader/material/renderer **没有释放**（进程马上退出，影响不大）

---

#### `src/config.h` 🚧

**职责**：**只做一件事**——选择渲染后端。

```cpp
#define OPENGL_RENDERER  // 定义宏，选择使用 OpenGL 渲染器
```

它已经**不再是"万能头"**（以前它把 glad / glfw / Shader / Mesh / Camera / Renderer 全 include 进来，现在那些都拆掉了）。

**已知问题**：
- 被 `Renderer.h` include → **后端选择宏污染了抽象层**（N1）
- 没有 `#pragma once`（N2）
- 严格来说只有工厂文件需要这个宏，`main.cpp` 不需要

---

### 5.2 抽象接口层

---

#### `src/Render/IShader.h` ✅

**职责**：着色器程序的抽象接口，用来解耦底层 API（OpenGL / Vulkan / DirectX）和上层渲染逻辑。

```cpp
class IShader {
public:
    virtual ~IShader() = default;

    virtual bool BuildFromFiles(const std::string& vertexPath,
                                const std::string& fragmentPath) = 0;  // 从文件构建
    virtual unsigned int GetID() const = 0;                            // ⚠️ 见下
    virtual void Use() = 0;
    virtual void SetMatrix(const glm::mat4& model,
                           const glm::mat4& view,
                           const glm::mat4& projection) = 0;
    virtual void SetLight(const glm::vec3& lightPos, const glm::vec3& lightColor) = 0;
    virtual void SetCamera(const glm::vec3& cameraPos) = 0;
};
```

**依赖**：`<string>`、`<glm/glm.hpp>`、`<glm/gtc/type_ptr.hpp>`（**不依赖任何图形 API 头**，干净的一层）

**已知问题**：
- `GetID()` 返回"着色器程序 ID"——这是 **OpenGL 特有的概念**（Vulkan/D3D 没有"program id"这种东西），属于抽象层泄漏。而且全项目目前没人调用它（N9）

---

#### `src/Render/IMesh.h` ✅

**职责**：网格的抽象接口。

```cpp
class IMesh {
public:
    virtual ~IMesh() = default;

    // 重载 1：直接给裸数组
    virtual void SetData(const float* vertices, int vertexCount,
                         const unsigned int* indices, int indexCount) = 0;

    // 重载 2：喂 ObjLoader 的结果
    virtual void SetData(const ObjMeshData& objMeshData) = 0;

    virtual void Draw() const = 0;
};
```

**依赖**：`ObjLoader.h`（→ `<string>` / `<vector>`）
**被谁实现**：`OpenGLMesh`
**被谁使用**：`RenderCommand`（`IMesh*`）、`OpenGLRenderer`（`mesh->Draw()`）、`main.cpp`

**已知问题**：
- `vertexCount` 的语义其实是"**float 个数**"而不是顶点数（`OpenGLMesh` 里用它 × `sizeof(float)` 传给 `glBufferData`），名字有歧义（N13）

---

### 5.3 渲染核心层

---

#### `src/Render/Renderer.h` ✅

**职责**：定义渲染后端的**抽象接口** `IRenderer`、一组内置渲染特性枚举，以及 `CreateRenderer` 的**声明**。

```cpp
enum class BuiltInRendererFeatures {
    DepthTest, Blend, CullFace, StencilTest, Multisample, ScissorTest
};

class IRenderer {
public:
    virtual ~IRenderer() = default;

    // ---- 数据 ----
    virtual glm::vec2 GetWindowSize() = 0;

    // ---- 窗口与上下文 ----
    virtual bool  Init() = 0;                       // 建窗口 + 上下文 + 加载 GLAD
    virtual void* GetWindow() = 0;                  // 原生窗口句柄（用 void* 避免暴露 GLFW）
    virtual void  SetClearColor(const glm::vec4& = glm::vec4(0.2f,0.3f,0.3f,1.0f)) = 0;
    virtual void  Clear() = 0;                      // 清颜色 + 深度

    // ---- 帧 ----
    virtual bool WindowShouldClose() = 0;
    virtual void WindowTerminate() = 0;             // 提前终止（内部就是 glfwTerminate）
    virtual void PollEvents() = 0;
    virtual void SwapBuffers() = 0;

    // ---- 渲染特性开关（底层逃生舱）----
    virtual void EnableRendererFeature(BuiltInRendererFeatures) = 0;
    virtual void DisableRendererFeature(BuiltInRendererFeatures) = 0;

    // ---- 资源创建（抽象工厂）----
    virtual IShader* CreateShader() = 0;
    virtual IMesh*   CreateMesh()   = 0;

    // ---- 渲染命令 ----
    virtual void ExecuteRenderCommands(const std::vector<RenderCommand>& cmds,
                                       const CameraData& cameraData) = 0;
};

// 自由函数：写在 class 外面（为什么？见 4.2）
IRenderer* CreateRenderer(int width, int height);
```

**依赖**：`RenderCommand.h`（→ `IMesh.h` / `Material.h`）、`IShader.h`、`IMesh.h`、`Camera.h`、`config.h` ⚠️
**被谁实现**：`OpenGLRenderer`（`VulkanRenderer` 占位）
**被谁使用**：`main.cpp`

**说明**：
- `CreateShader()` / `CreateMesh()` 是**抽象工厂**做法：把"资源的创建"也收进后端，这样上层连 `OpenGLShader` 都不用认识。
- 以前的遗留接口 `Render(Mesh*, Shader*, glm::mat4&)`（空实现）**已经删掉**。

**已知问题**：
- `#include "config.h"` 让抽象层认识"后端选择宏"（N1）
- `EnableRendererFeature` / `DisableRendererFeature` **会绕过状态缓存**（见 7.3），是个定时炸弹
- `GetWindow()` 返回 `void*`，谁用都得自己转回来

---

#### `src/Render/RendererFactory.h` ✅ / `RendererFactory.cpp` ✅

**职责**：**全项目唯一知道所有后端的装配点**。

```cpp
// RendererFactory.h
#pragma once
#include "Renderer.h"          // ⚠️ 间接带来 config.h —— 隐式依赖，见 N3

#if defined(OPENGL_RENDERER)
    #include "OpenGL/OpenGLRenderer.h"
#elif defined(VULKAN_RENDERER)
    #include "Vulkan/VulkanRenderer.h"
#endif
```

```cpp
// RendererFactory.cpp
#include "RendererFactory.h"

IRenderer* CreateRenderer(int width, int height) {
    #if defined(OPENGL_RENDERER)
        return new OpenGLRenderer(width, height);
    #elif defined(VULKAN_RENDERER)
        return new VulkanRenderer(width, height);
    #else
        return nullptr;                    // ⚠️ 见 N4：建议改成 #error
    #endif
}
```

**依赖**：`Renderer.h` + 所有后端的头（有条件）
**被谁使用**：`main.cpp`（间接，通过 `Renderer.h` 的声明）
**说明**：这个 `.h` 目前**只被 `RendererFactory.cpp` 一个文件 include**，本质上是工厂的私有实现头。

**已知问题**：
- 对 `config.h` 是隐式依赖（N3）
- `#else` 返回 `nullptr` 而不是 `#error`（N4）
- 后端清单在 `.h` 和 `.cpp` 里各一份，要同步（N11）
- include 风格与其它文件不一致（N10）

---

#### `src/Render/RenderCommand.h` ✅

**职责**：定义"一条渲染命令"需要的最小数据。

```cpp
struct Transform {          // 物体的 TRS
    glm::vec3 position;
    glm::vec3 rotation;     // 欧拉角（度）
    glm::vec3 scale;
};

struct RenderCommand {      // 一条"请把这个东西画出来"的命令
    IMesh*    mesh;         // 画什么几何（★ 已从 Mesh* 升级为 IMesh*）
    Material* material;     // 用什么材质（= IShader + 渲染状态）
    Transform transform;    // 放在哪里
};
```

**设计要点**：
- 只存 **Transform**，不存算好的 `mat4` —— 避免同一个 model 变换出现两个真相源
- 渲染时按 **T → R → S** 的顺序后乘（GLM 的 `translate/rotate/scale` 都是后乘）
- `mesh` / `material` **都是不拥有所有权的裸指针**

**依赖**：`IMesh.h`、`Material.h`、`<glm/glm.hpp>`
**被谁使用**：`Renderer.h`（接口参数）、`main.cpp`（组装队列）、`OpenGLRenderer`（执行）

---

#### `src/Render/Material.h` / `Material.cpp` ✅

**职责**：把「用哪个 Shader」和「用什么渲染状态」绑在一起 —— 这就是"材质"的本质。

```cpp
// ---- 渲染状态：与图形 API 无关的"意图" ----
enum class DepthFunc { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class CullMode  { Off, Front, Back };
enum class BlendMode { Opaque, AlphaBlend, Additive, Multiply };

struct RenderState {
    bool      depthTest  = false;             // ★ 默认值 = GL 的真实初始状态（见下）
    bool      depthWrite = true;
    DepthFunc depthFunc  = DepthFunc::Less;
    CullMode  cullMode   = CullMode::Off;     // ★ 同上
    BlendMode blend      = BlendMode::Opaque;

    bool operator<(const RenderState& o) const;   // 排序依据：减少状态切换
};

// ---- 材质 ----
class Material {
public:
    Material(IShader* shader);
    ~Material();

    void     SetShader(IShader*);
    IShader* GetShader() const;      // ★ 已从 Shader* 升级为 IShader*
    RenderState renderState;         // 公开数据，上层直接赋值即可配置

private:
    IShader* m_Shader = nullptr;     // ★ 不拥有所有权，只引用！（建议加 explicit 与注释）
};
```

**依赖**：`IShader.h`（**不再间接依赖 glad** —— 这是本轮修好的一处分层泄漏 ✅）
**被谁使用**：`RenderCommand`、`OpenGLRenderer`

**⚠️ 关于 `RenderState` 的默认值（重要）**

默认值被**刻意**设成了 **OpenGL 的真实初始状态**：`depthTest = false`、`cullMode = Off`、`blend = Opaque`。
这样 `OpenGLRenderer` 里的状态缓存（`mRenderState`）从一开始就"说的是真话"。

**代价**：`Material` 的默认状态是**"不测深度、不剔面"**。所以**新建材质时一定要显式写全状态**：

```cpp
material.renderState.depthTest = true;    // 不写的话，默认是不测深度的！
```

---

#### `src/Render/Camera/Camera.h` / `Camera.cpp` ✅

**职责**：轨道摄像机。不直接存位置，而是用**球坐标**三个数描述它在哪里，每帧换算成 View / Projection 矩阵。

```cpp
struct CameraData {           // 交给渲染后端的"相机数据包"
    glm::vec3 position;
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
};

class Camera {
public:
    // ---- 输入状态（由上层 / 回调写入）----
    bool   dragging = false;
    double mouseX = 0.0, mouseY = 0.0;

    // ---- 视口 ----
    void  SetViewportSize(const glm::vec2& size);   // 同时更新 aspectRatio（带除零保护）
    float aspectRatio = 800.0f / 600.0f;            // public；viewportSize 是 private

    // ---- 轨道参数（球坐标）----
    float camYaw     = 45.0f;    // 水平角（度）
    float camPitch   = 20.0f;    // 俯仰角（度，会被夹紧到 ±89°）
    float camRadius  = 8.0f;     // 距原点的距离
    float sensitivity = 0.25f;   // 每像素鼠标位移改变多少度

    void BeginDrag(double x, double y);   // 左键按下：置 dragging，并对齐 lastX/lastY
    void EndDrag();                        // 左键松开
    void Update();                         // 每帧调用：球坐标 → position / view / projection

    CameraData GetCameraData() { return mCameraData; }
};
```

**关键点**：
- **始终看向原点**
- **不依赖窗口、不依赖渲染后端** —— `aspect` 由上层用 `SetViewportSize` 喂进来
- 构造函数里就调一次 `Update()`，保证第一帧就有有效的矩阵
- `SetViewportSize` 里对 `size.y == 0`（窗口最小化）做了保护，避免除零产生 `inf` 污染投影矩阵

**依赖**：只依赖 `<glm/glm.hpp>` 和 `<glm/gtc/matrix_transform.hpp>`（干净的一层）
**被谁使用**：`main.cpp`、`Renderer.h`（为了 `CameraData` 类型）

**已知问题**：
- `Camera.cpp` 里 **`if (dragging)` 被注释掉了** → 每帧无条件套用鼠标位移；配合 main 里的 `mouseX += 1.0` 造成持续自转
- 目前**没有任何地方注册鼠标回调**，所以 `mouseX / dragging` 正常情况下永远不会被写入
- `aspectRatio` 是 public 而 `viewportSize` 是 private，风格不一致；其实 `aspectRatio` 是派生值，两者容易不同步

---

### 5.4 OpenGL 实现层（唯一碰图形 API 的地方）

---

#### `src/Render/OpenGL/OpenGLRenderer.h` / `OpenGLRenderer.cpp` ✅

**职责**：`IRenderer` 的 OpenGL 实现。

**公开成员**：

| 成员 | 说明 |
|---|---|
| `GLFWwindow* window` | public 的窗口句柄（建议改 private） |
| `Init()` | → `CreateWindow()` |
| `GetWindowSize()` | 返回 `glm::vec2(WINDOW_WIDTH, WINDOW_HEIGHT)`（**固定值，不随 resize 变化**） |
| `GetWindow()` | 返回 `void*` 窗口句柄 |
| `WindowShouldClose()` / `PollEvents()` / `SwapBuffers()` / `WindowTerminate()` | GLFW 转发 |
| `SetClearColor(color)` | `glClearColor` |
| `Clear()` | 先 `glDepthMask(GL_TRUE)` + 同步缓存，再 `glClear(颜色 \| 深度)` ✅（坑已修） |
| `EnableRendererFeature` / `DisableRendererFeature` | `glEnable` / `glDisable`（⚠️ 绕过状态缓存） |
| `CreateShader()` / `CreateMesh()` | `new OpenGLShader()` / `new OpenGLMesh()` |
| `ExecuteRenderCommands(cmds, cameraData)` | 渲染主入口，见下 |

**私有成员**（状态系统的核心）：

| 成员 | 说明 |
|---|---|
| `mRenderState` | **状态缓存**：记录"上一次真正发给 GL 的状态" |
| `ApplyRenderState(state)` | 把材质想要的状态翻译成 GL 调用（**只在变化时才发**） |
| `RenderStateToOpenGL(DepthFunc/CullMode/BlendMode)` | 枚举 → GLenum 翻译（3 个重载） |
| `ConvertBuiltInRendererFeaturesToGLenum(feature)` | 渲染特性枚举 → GLenum |
| `CreateWindow()` | glfwInit → window hints → create window → makeCurrent → gladLoadGLLoader |

**`ExecuteRenderCommands` 的执行流程**：

```
glEnable(GL_MULTISAMPLE);

for (每条命令 command) {
    command.material->GetShader()->Use();              // 1. 启用着色器程序（IShader*）
    ApplyRenderState(command.material->renderState);   // 2. 应用渲染状态（带缓存，只在变化时发 GL 调用）

    // 3. 由 Transform 算出 Model 矩阵：T → R(x,y,z) → S
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    modelMatrix = glm::translate(modelMatrix, transform.position);
    modelMatrix = glm::rotate   (modelMatrix, transform.rotation.x, {1,0,0});
    modelMatrix = glm::rotate   (modelMatrix, transform.rotation.y, {0,1,0});
    modelMatrix = glm::rotate   (modelMatrix, transform.rotation.z, {0,0,1});
    modelMatrix = glm::scale    (modelMatrix, transform.scale);

    // 4. 上传 uniform
    shader->SetMatrix(modelMatrix, cameraData.viewMatrix, cameraData.projectionMatrix);
    shader->SetLight({0.5f,1.0f,0.2f}, {1.0f,1.0f,1.0f});   // ← 光源是写死的
    shader->SetCamera(cameraData.position);

    // 5. 画
    command.mesh->Draw();                              // IMesh::Draw() const
}
```

**依赖**：`Renderer.h`、`OpenGLShader.h`、`OpenGLMesh.h`、`<glad/glad.h>`、`<GLFW/glfw3.h>`
**被谁使用**：由 `RendererFactory.cpp` 创建，以 `IRenderer*` 的形式交给 `main`

**已知问题**：
- `window` 是 public
- 光源参数写死在循环里（N5）
- 渲染队列没有任何排序（N6）

---

#### `src/Render/OpenGL/OpenGLShader.h` / `OpenGLShader.cpp` ✅

**职责**：`IShader` 的 OpenGL 实现。

```cpp
class OpenGLShader : public IShader {
public:
    OpenGLShader();                                  // 默认构造，此时 m_ID == 0
    ~OpenGLShader() override;                        // glDeleteProgram(m_ID)
                                                     // ⚠️ 要求 GL 上下文仍存活
    unsigned int GetID() const override;             // { return m_ID; }
    void Use() override;                             // glUseProgram(m_ID)
    void SetMatrix(model, view, projection) override;
    void SetLight(lightPos, lightColor) override;
    void SetCamera(cameraPos) override;
    bool BuildFromFiles(vs, fs);                     // ★ 满足 IShader 的纯虚，但没写 override

private:
    unsigned int m_ID = 0;
    const std::string mVertexPath;                   // ⚠️ 已无人使用（N7）
    const std::string mFragmentPath;                 // ⚠️ 同上

    std::string  readShaderFile(const std::string& path);
    unsigned int compileShader(GLenum type, const std::string& source);
};
```

**BuildFromFiles 的流程**：读两个文件 → 各自 `compileShader` → `glCreateProgram` + attach + link → 检查 `GL_LINK_STATUS` 并打印日志 → `glDeleteShader` 两个中间对象 → 返回是否成功。

**依赖**：`IShader.h`、`<glad/glad.h>`、`<GLFW/glfw3.h>`、`<fstream>` / `<sstream>` / `<string>` / `<iostream>`
**被谁使用**：`OpenGLRenderer::CreateShader()` 创建；`OpenGLRenderer::ExecuteRenderCommands` 通过 `IShader*` 调用

**已知问题**：
1. **`BuildFromFiles` 没写 `override`**（N8）——现在靠签名完全一致隐式覆盖。加 `override` 能让"签名写错"变成编译错误
2. `mVertexPath` / `mFragmentPath` 是早期"构造时构建"留下的死成员（N7）
3. **每次 `SetMatrix` / `SetLight` / `SetCamera` 都调用 `glGetUniformLocation`**（字符串查找 + 驱动调用）。location 从链接成功那一刻就不会再变，应该查一次缓存起来。100 个物体 × 5 个 uniform = 每帧 500 次无谓查找
4. 构造时 `m_ID = 0`，如果不调 `BuildFromFiles` 就直接 `Use()`，会执行 `glUseProgram(0)`（无害但静默）
5. 构造失败无法上报：错误只能通过 `BuildFromFiles` 的返回值传递，而 main 现在**丢弃了它**（N8/N9）

---

#### `src/Render/OpenGL/OpenGLMesh.h` / `OpenGLMesh.cpp` ✅

**职责**：`IMesh` 的 OpenGL 实现，封装一份 GPU 网格数据（VAO + VBO + EBO）。

**接口**：

| 成员 | 说明 |
|---|---|
| `OpenGLMesh()` | 三个句柄初始化为 0 |
| `~OpenGLMesh()` | `glDeleteVertexArrays` + `glDeleteBuffers` |
| `SetData(const float* vertices, int vertexCount, const unsigned int* indices, int indexCount)` | 重载 1：直接给裸数组 |
| `SetData(const ObjMeshData&)` | 重载 2：喂 `ObjLoader` 的结果（main 用的是这个） |
| `Draw() const` | `glBindVertexArray` + `glDrawElements(GL_TRIANGLES, mIndexCount, GL_UNSIGNED_INT, 0)` |

**私有**：`VAO` / `VBO` / `EBO` / `mIndexCount` / `setDataInternal(...)`

**顶点属性布局**（由 `setDataInternal` 设定，**着色器必须对得上**）：

| location | 属性 | 分量 | 偏移 | 字节 |
|---|---|---|---|---|
| 0 | 位置（aPos） | 3 × float | 0 | 12 |
| 1 | 法线（aNormal） | 3 × float | 12 | 12 |
| 2 | UV（aTexCoor） | 2 × float | 24 | 8 |
| — | **步长（stride）** | — | — | **32** |

**依赖**：`IMesh.h`、`<glad/glad.h>`
**被谁使用**：由 `OpenGLRenderer::CreateMesh()` 创建；`OpenGLRenderer::ExecuteRenderCommands` 通过 `IMesh*` 调用

**已知问题**：
1. **同一个对象调两次 `SetData` 会泄漏**：`setDataInternal` 每次都 `glGen*` 并覆盖句柄，旧 VAO/VBO/EBO 不会被删除（N12）
2. `ObjMeshData` 重载里从 `.size()` 直接转 `int`，顶点数超过 `INT_MAX` 才会出问题（实际不会）
3. 文件里的注释还留着早期"每个顶点 6 个 float / stride 24"的说法，实际已经是 **8 个 float / stride 32**

---

#### `src/Render/Vulkan/VulkanRenderer.h` ⬜（占位）

**职责**：Vulkan 后端的占位声明，完整复刻了 `IRenderer` 的接口清单，用于验证"加第二个后端时架构是否成立"。

**现状**：**只有 `.h`，没有 `.cpp`，没有任何实现**。

**⚠️ 注意**：一旦把宏切成 `#define VULKAN_RENDERER`，`new VulkanRenderer(width, height)` 会在**链接期**报一堆 `undefined reference`（构造、析构、每个成员都没有定义）。建议在文件头标注 `// TODO: 未实现`，或先把它从 CMake 清单里拿掉（N14）。

---

### 5.5 工具与资源

---

#### `src/ObjLoader.h` / `ObjLoader.cpp` ✅

**职责**：读 `.obj` 模型，并做"顶点展开"——把 OBJ 里**三套独立索引**（位置 v / UV vt / 法线 vn）转成 GPU 需要的**单一索引**格式。

**数据结构**：

```cpp
struct ObjMeshData {
    std::vector<float>         vertices;  // 交错：每顶点 8 个 float
                                          //   位置 xyz | 法线 nxyz | UV uv
    std::vector<unsigned int>  indices;   // 每 3 个 = 一个三角形

    int VertexCount() const;              // = vertices.size() / 8
    int IndexCount()  const;              // = indices.size()
};
```

**接口**：`bool LoadObj(path, out)`、`void PrintObjData(data)`

**依赖**：`tinyobjloader`（`ObjLoader.cpp` 里 `#define TINYOBJLOADER_IMPLEMENTATION` 后 include，实现只编译一次）
**被谁使用**：`main.cpp`（加载 monkey.obj / plane.obj）、`IMesh.h`（类型）

> 加载结果里如果某个顶点没有法线或 UV，对应分量填 0。
> 加载时会往控制台打印统计信息（位置数/法线数/UV 数/展开后顶点数/三角形数）。

---

#### `src/Resources/ResourceManager.h` ⬜（空文件）

**规划职责**：单例，负责 Shader / Mesh / Texture 的加载与缓存。
**现状**：空。将来 `IShader` / `IMesh` 的生命周期应该由它负责（因为 `Material` 只引用不拥有）。

---

### 5.6 文档

| 文件 | 说明 |
|---|---|
| `src/目标渲染架构.md` | 架构规划：分层与依赖规则、矩阵归属、主循环顺序、接口草图、目录现状、下一步优先级。**注意其中部分内容已过时**（例如 `Shader*`、`config.h` 万能头的描述） |
| `src/Core/想法.md` | 随手记的疑问，作为沟通渠道用（例如"是不是该有个 Manager？""矩阵该放哪里？"） |

---

## 六、文件之间的关系

### 6.1 Include（编译期依赖）

现在**没有任何"万能头"**，每个文件只 include 自己需要的：

```
main.cpp
  ├─→ <iostream>
  ├─→ Camera.h ────────────→ glm（唯一一个完全干净的头）
  └─→ Renderer.h ──→ config.h                     ← ⚠️ N1：抽象层认识后端宏
        │             RenderCommand.h ──→ IMesh.h ──→ ObjLoader.h
        │                                  Material.h ──→ IShader.h ──→ glm
        │             IShader.h
        │             IMesh.h
        │             Camera.h
        └─→ （CreateRenderer 的声明就来自 Renderer.h）

RendererFactory.cpp
  └─→ RendererFactory.h
        ├─→ Renderer.h ──→ …（同上，最重要的是带来 config.h 的宏）
        └─→ OpenGL/OpenGLRenderer.h  ← 只在 OPENGL_RENDERER 定义时才 include
              └─→ <glad/glad.h> / <GLFW/glfw3.h> / OpenGLShader.h / OpenGLMesh.h

OpenGLRenderer.cpp ──→ OpenGLRenderer.h（+ OpenGLShader.h / OpenGLMesh.h）
OpenGLShader.cpp   ──→ OpenGLShader.h ──→ IShader.h
OpenGLMesh.cpp     ──→ OpenGLMesh.h   ──→ IMesh.h ──→ ObjLoader.h
Material.cpp       ──→ Material.h ──→ IShader.h
Camera.cpp         ──→ Camera.h
ObjLoader.cpp      ──→ ObjLoader.h + tinyobjloader
```

**读法**：只有 `Render/OpenGL/` 底下的文件会连到 `<glad/glad.h>` / `<GLFW/glfw3.h>`。
**`main.cpp` 已经完全不连到 glad/GLFW 了** —— 这就是本轮重构最直观的成果。

> 仍然存在的两条"漏"：`Renderer.h → config.h`（N1），以及 `RendererFactory.h` 靠 `Renderer.h` 间接拿宏（N3）。

### 6.2 对象拥有关系（运行期）

```
main()
 ├── IRenderer* renderer ──(CreateRenderer 里 new / main 里 delete)──> OpenGLRenderer
 │        │                                                             └── 拥有 → GLFWwindow
 │        │                                                             └── 内嵌 mRenderState（状态缓存）
 │        │
 │        ├── CreateShader() ──> IShader*（实际是 OpenGLShader，拥有 GL program）
 │        └── CreateMesh()   ──> IMesh*  （实际是 OpenGLMesh，拥有 VAO/VBO/EBO）
 │                    ▲
 │                    │ 引用（不拥有）
 │              Material*（栈对象 material / planeMaterial）
 │                    ▲
 │                    │ 引用（不拥有）
 │              RenderCommand{ IMesh*, Material*, Transform }
 │
 ├── Camera camera            （栈对象，纯数据 + 几个方法）
 └── std::vector<RenderCommand> renderQueue   （全是指针，不拥有）
```

> **所有权规则**：
> 1. **谁创建谁销毁** —— 资源由 `renderer->CreateShader/CreateMesh()` 创建，由 `main` `delete`（接口都有虚析构，`delete IShader*` 是安全的）。
> 2. `Material` 和 `RenderCommand` **只引用不拥有**，所以它们的生命周期必须短于被引用的对象。
> 3. **⚠️ 销毁顺序**：`~OpenGLShader` 会 `glDeleteProgram`，`~OpenGLMesh` 会 `glDelete*`，这些都**要求 GL 上下文仍然存活**。所以必须**先删资源、最后删 renderer**（因为 `~OpenGLRenderer` 里会 `glfwTerminate`）。main 现在的顺序是对的。
>    将来若改成"renderer 负责销毁资源"，那销毁动作要放进 `~OpenGLRenderer` 里、`glfwTerminate()` **之前**。

### 6.3 一帧的数据流

```
   main 主循环
       │
       ├─① renderer->PollEvents()          → GLFW 处理输入（回调会写 camera 的鼠标状态）
       │
       ├─② camera.Update()                → 球坐标换算成 View / Projection，存进 mCameraData
       │
       ├─③ renderer->Clear()              → 先 glDepthMask(TRUE) 同步缓存，再 glClear(颜色 | 深度)
       │
       └─④ renderer->ExecuteRenderCommands(renderQueue, camera.GetCameraData())
              │
              │   renderQueue: [ {mesh, material, transform}, {plane, planeMaterial, transform} ]
              │   cameraData : { position, viewMatrix, projectionMatrix }
              │
              ▼
        OpenGLRenderer::ExecuteRenderCommands     ← 唯一的 gl* 现场
              │
              └─（逐条命令）
                    ├─ material->GetShader()->Use()          → glUseProgram   （IShader*）
                    ├─ ApplyRenderState(material->renderState)
                    │      └─ 与 mRenderState 比较，只把"变化的部分"翻译成
                    │         glEnable/glDisable/glDepthMask/glDepthFunc/glCullFace/glBlendFunc
                    ├─ 算 Model 矩阵（T→R→S）
                    ├─ shader->SetMatrix / SetLight / SetCamera  → glUniform*
                    └─ mesh->Draw()                          → glBindVertexArray + glDrawElements
              │
              ▼
        renderer->SwapBuffers()            → glfwSwapBuffers（呈现）
```

---

## 七、关键概念

### 7.1 渲染命令（RenderCommand）

一条命令 = **"画什么（mesh）+ 用什么（material）+ 放在哪（transform）"**。

它的意义是**把"收集"和"执行"解耦**：

- `main` / 将来的 Scene 层负责**收集**（遍历场景、剔除、排序）→ 产出一串命令
- 渲染后端负责**执行**（逐条翻译成 GL 调用）

这样后端完全不需要知道"场景里有什么"，换后端时收集逻辑一行都不用改。

### 7.2 材质（Material）= IShader + 渲染状态

为什么要单独有个 `Material`，而不是让 `RenderCommand` 直接指向 `IShader`？

| 数据 | 谁决定 | 变化频率 | 放哪 |
|---|---|---|---|
| mesh（几何） | 物体 | 几乎不变 | `RenderCommand` |
| transform | 物体 | 每帧可能变 | `RenderCommand` |
| **shader 程序** | **材质** | 不变 | `Material` |
| **depth / blend / cull 状态** | **材质** | 不变 | `Material` |
| view / projection | 相机 | 每帧一次 | `CameraData` |
| 光源 | 场景 | 每帧一次 | 目前写死在渲染循环里（待改进） |

关键在于：**渲染状态不是"某个物体的数据"，而是"接下来这批 draw call 用什么规则画"**。
把它绑在物体上会导致冗余和状态反复横跳，绑在 Shader 上又不对（同一个 shader 可以配不同状态）。

### 7.3 渲染状态缓存（最容易出 bug 的地方）

`glEnable` / `glDisable` 这类调用改的是**上下文的全局状态**，而且**发出去就要花驱动的钱**。
所以 `OpenGLRenderer` 用 `mRenderState` 缓存"上一次真正设过的状态"，只在变化时才发调用：

```cpp
if (renderState.depthTest != mRenderState.depthTest) {
    if (renderState.depthTest) glEnable(GL_DEPTH_TEST);
    else                       glDisable(GL_DEPTH_TEST);
    mRenderState.depthTest = renderState.depthTest;   // ★ 必须同步缓存
}
```

> **铁律：`mRenderState` 是 GL 真实状态的"镜像"。它和实际状态一旦不一致，之后所有 `!=` 比较都会失真。**
>
> 所以只有两个地方能写它：
> 1. **初始化时**按 GL 的真实初始状态填一次（现在通过 `RenderState` 的默认值实现）
> 2. **每次真的发出 GL 调用之后**立刻同步
>
> 其它任何地方（例如 `EnableRendererFeature`、帧与帧之间）都**不准**改它。

> **另一个经典陷阱（已修复 ✅）**：`glClear(GL_DEPTH_BUFFER_BIT)` **受 `glDepthMask` 控制**！
> 如果上一帧最后把 `depthWrite` 设成 `false`，下一帧的 `Clear()` 就**清不掉深度缓冲**。
> 现在的 `Clear()` 开头会先 `glDepthMask(GL_TRUE)` 并同步缓存，问题已解决。

### 7.4 矩阵归属

| 矩阵 | 由谁算 | 存在哪 | 怎么到 GPU |
|---|---|---|---|
| **Model** | `Transform`（位置/旋转/缩放） | `RenderCommand::transform` | `ExecuteRenderCommands` 里按 T→R→S 现算 |
| **View** | `Camera::Update()` | `CameraData::viewMatrix` | 每帧随 `CameraData` 传给后端 |
| **Projection** | `Camera::Update()`，`aspect` 由 `SetViewportSize` 喂入 | `CameraData::projectionMatrix` | 同上 |

**关键原则**：
- 相机**不主动去问窗口大小** —— 让逻辑层反向依赖平台层是错的
- 窗口大小的**真相源在渲染后端**（只有它知道 framebuffer 尺寸），由**上层**转发给相机：

```cpp
camera.SetViewportSize(renderer->GetWindowSize());   // 位置：上层
```

- 后端**不存** view/projection 的语义，它只是"这一帧传进来的那一份数据"

### 7.5 渲染循环的顺序（为什么这么排）

```
① PollEvents()          ← 先处理输入。鼠标回调是它触发的，反了的话相机晚一帧
② camera.Update()       ← 再更新逻辑（读回调刚写进去的鼠标状态）
③ Clear()               ← 清屏（注意 depth mask 的坑，见 7.3）
④ ExecuteRenderCommands ← 收集好的命令 + 相机数据
⑤ SwapBuffers()         ← 呈现
```

**原则**：`Update` 属于**逻辑层**，只改数据、**不做任何 GL 调用**；渲染的事全交给后端。

### 7.6 透明物体

一个半透明物体（比如地面网格）要正确工作，通常需要"三件套"：

```cpp
renderState.depthTest  = true;    // 测深度：让远处的部分被前面的不透明物体挡住
renderState.depthWrite = false;   // 不写深度：不要挡住后面画的东西
renderState.blend      = BlendMode::AlphaBlend;  // 开混合
```

再加上**绘制顺序**：**透明物体必须排在所有不透明物体之后**（物体多了还要按距离从远到近排）。

> **"测深度"和"写深度"是两个独立开关**，不要混为一谈。
> 深度写入对应的是 `glDepthMask(GL_TRUE/FALSE)`，**不是** `glEnable/glDisable(GL_DEPTH_TEST)`。

---

## 八、Shader 与顶点格式约定

### 8.1 顶点属性（由 `OpenGLMesh` 设定，着色器必须一致）

```glsl
layout (location = 0) in vec3 aPos;      // 位置，偏移 0 字节
layout (location = 1) in vec3 aNormal;   // 法线，偏移 12 字节
layout (location = 2) in vec2 aTexCoor;  // UV，  偏移 24 字节
// 步长（stride）= 32 字节
```

### 8.2 Uniform 约定

| uniform | 类型 | basicvertex / basicfrag | groundNetVertex / groundNetFrag | 由谁设置 |
|---|---|---|---|---|
| `ModelMatrix` | mat4 | ✔ | ✔ | `IShader::SetMatrix` |
| `ViewMatrix` | mat4 | ✔ | ✔ | `IShader::SetMatrix` |
| `ProjectionMatrix` | mat4 | ✔ | ✔ | `IShader::SetMatrix` |
| `mainLightPos` | vec3 | ✔ | ✔ | `IShader::SetLight` |
| `mainLightColor` | vec3 | ✔ | ✔ | `IShader::SetLight` |
| `CameraPos` | vec3 | ✖ | ✔ | `IShader::SetCamera` |

> 对着没有声明该 uniform 的 shader 调 `SetCamera` 是无害的：
> `glGetUniformLocation` 会返回 -1，而 `glUniform*` 在 location 为 -1 时**被规范要求忽略**。

### 8.3 两个着色器对

**`basicvertex.glsl` / `basicfrag.glsl`** —— 猴头用
- 顶点：`gl_Position = P * V * M * vec4(aPos, 1.0)`，把法线直接传给片元
- 片元：半兰伯特光照 `lambert = dot(N, normalize(lightPos)) * 0.5 + 0.5`，输出 **alpha = 1.0（不透明）**

**`groundNetVertex.glsl` / `groundNetFrag.glsl`** —— 地面网格用
- 顶点：除了 `gl_Position`，还输出 `vertexPos`（模型空间位置）和 `posWS`（世界空间位置）
- 片元：用 `fwidth` 做**屏幕空间抗锯齿**的无限网格：
  - 小网格（间距 0.01）/ 大网格（间距 0.1）/ 红轴（x 方向）/ 蓝轴（z 方向）
  - 用 `distance(CameraPos, posWS)` 做**距离淡出**（远处不画，省性能也好看）
  - 输出 `fragColor = vec4(netColor, alpha)`，其中大部分像素接近全透明

---

## 九、已知问题与 TODO

### 9.1 渲染状态与绘制（优先级最高）

| # | 问题 | 后果 | 建议 |
|---|---|---|---|
| R1 | `RenderStateToOpenGL(BlendMode)` 返回单个值，却用来喂 `glBlendFunc` 的两个参数 | `AlphaBlend` 恰好正确；**`Additive` / `Multiply` 是错的** | 改成 `switch`，一个 case 里同时设 `glBlendFunc(src, dst)` |
| R2 | `EnableRendererFeature` / `DisableRendererFeature` 直接 `glEnable`/`glDisable` | **绕过状态缓存** → 缓存失真 → 之后所有状态判断都错 | 删掉 main 里的调用；或让它们同步更新 `mRenderState` |
| R3 | 渲染队列没有排序 | 透明物体必须最后画，现在只是"恰好"顺序对 | 收集阶段按 `RenderState::operator<` 排序，透明物体排最后 |
| R4 | 光源参数写死在 `ExecuteRenderCommands` 里 | 每个物体都重复上传同一份光照；无法配置 | 挪进"帧数据"结构（如 `FrameData`），每帧传一次 |
| R5 | 每次 `SetMatrix/SetLight/SetCamera` 都 `glGetUniformLocation` | 每帧几百次无谓的字符串查找 | 链接成功后查一次并缓存 location |

> ✅ **已修复**：`Clear()` 的深度 mask 问题（现在会先 `glDepthMask(GL_TRUE)` 并同步缓存）。

### 9.2 摄像机与输入

| # | 问题 | 建议 |
|---|---|---|
| C1 | `Camera.cpp` 里 `if (dragging)` 被注释掉 | 恢复它，否则"不按左键光移动鼠标"也会旋转 |
| C2 | `main.cpp` 里 `camera.mouseX += 1.0;` | 删掉（这是让相机自转的调试代码） |
| C3 | 没有任何鼠标回调注册 | 需要 `glfwSetCursorPosCallback` / `glfwSetMouseButtonCallback` → 转发给 `Camera::BeginDrag/EndDrag`。**建议后端只暴露"回调注册"接口，由上层转发**，不要在 `OpenGLRenderer` 里直接认识 `Camera` |
| C4 | 没有处理窗口 resize | 注册 `glfwSetFramebufferSizeCallback`，回调里 `glViewport` + 更新相机 aspect |
| C5 | `GetWindowSize()` 返回的是构造时的固定值 | 改成 `glfwGetFramebufferSize(window, ...)`，否则拉伸窗口画面会变形 |
| C6 | `aspectRatio` / `viewportSize` 访问权限不一致且可能不同步 | 统一成 private + 只有一个入口 |

### 9.3 架构与解耦（本轮的遗留）

| # | 问题 | 后果 / 触发条件 | 建议 |
|---|---|---|---|
| **N1** | `Renderer.h:3` include 了 `config.h` | **后端选择宏污染抽象层**：改一个宏要重编全项目；抽象层里出现 `OPENGL_RENDERER` 这个名字 | 把 `#include "config.h"` 从 `Renderer.h` 挪走，只留在工厂文件 |
| **N2** | `config.h` 没有 `#pragma once` | 现在只有一行宏，重复定义合法；以后加第一个 `enum`/`struct` 就会重复定义报错 | 加一行 `#pragma once` |
| **N3** | `RendererFactory.h` 对 `config.h` 是**隐式依赖**（靠 `#include "Renderer.h"` 间接拿到宏） | **一旦做了 N1，这里立刻判假** → 一个后端头都不 include → 报 `'OpenGLRenderer' was not declared` | 在 `RendererFactory.h` 的 `#if` 之前补 `#include "config.h"`（谁用谁 include） |
| **N4** | 工厂 `#else` 分支返回 `nullptr`，且 `main` 不检查 | 宏没配好 → `renderer->Init()` 空指针虚调用 → **`0xC0000005` 崩溃，窗口都出不来**（已实测踩过一次） | 工厂里改成 `#error "没有选择渲染后端"`；同时 main 里加 `if (!renderer) { ... return -1; }` |
| **N5** | 后端清单在 `RendererFactory.h` 和 `RendererFactory.cpp` 里各一份 | 加后端要改两处，漏了会出诡异错误 | 可接受；或让 `.h` 只负责 include、`.cpp` 只负责分支，并写注释互相提醒 |
| **N6** | `OpenGLShader::BuildFromFiles` 没写 `override` | 签名写错时不会报错，只会静默不覆盖（变成抽象类或调不到） | 加上 `override` |
| **N7** | `OpenGLShader` 的 `const mVertexPath / mFragmentPath` 已无人使用 | 死成员，占空间也误导 | 删掉 |
| **N8** | `main` 丢弃 `BuildFromFiles` 的返回值 | shader 加载失败时静默继续，之后画面全黑很难查 | 检查返回值，失败就打印并退出 |
| **N9** | `IShader::GetID()` 把"program id"这个 OpenGL 概念放进抽象接口 | Vulkan/D3D 没有这种概念，抽象层泄漏；而且全项目没人调用 | 删掉，或改成不暴露底层句柄的语义 |
| **N10** | include 风格不一致（`"OpenGL/OpenGLRenderer.h"` vs `"OpenGLRenderer.h"`） | 读代码时容易迷惑（一个靠 `-Isrc/Render`，一个靠 `-Isrc/Render/OpenGL`） | 统一一种 |
| **N11** | `IRenderer` 接口偏胖（窗口 + 资源工厂 + 渲染命令混在一起） | 将来容易继续膨胀 | 早晚要拆成 `IDevice`（资源工厂）/ `IRenderer`（帧）两类 |
| **N12** | `VulkanRenderer.h` 只有头没有 `.cpp` | 切到 `VULKAN_RENDERER` 会链接失败 | 标 `// TODO: 未实现`，或先从 CMake 清单移除 |
| **N13** | `IRenderer::GetWindow()` 返回 `void*` | 调用方必须自己转回来，类型不安全 | 需要时引入前向声明的句柄类型 |

> ✅ **已修复**：`Shader` / `Mesh` 直接调 `gl*` 且放在 `src` 根目录（已拆成 `IShader`/`IMesh` + `Render/OpenGL/*`）；
> `Material` 间接依赖 GL（现在只 include `IShader.h`）；`IRenderer::Render(Mesh*,Shader*,mat4&)` 空实现（已删除）；
> `config.h` 万能头（已瘦身成一行宏）。

### 9.4 资源生命周期与内存

| # | 问题 | 后果 | 建议 |
|---|---|---|---|
| M1 | `OpenGLMesh::SetData` 重复调用会 `glGen*` 并覆盖旧句柄 | 旧的 VAO/VBO/EBO 泄漏 | 在 `setDataInternal` 开头先删旧的，或加"只允许设置一次"的断言 |
| M2 | `main` 提前 `return -1` 的路径不释放已创建的资源 | 泄漏（进程即将退出，影响小） | 用栈对象 / 智能指针，或收进一个 `Application` 类 |
| M3 | `IMesh::SetData` 的 `vertexCount` 语义是"float 个数" | 名字有歧义，容易传错 | 改名 `floatCount`，或改成"顶点数 + stride" |
| M4 | 资源生命周期全靠 `main` 手工 `delete`，顺序错了就用已销毁的上下文调 `glDelete*` | 句柄泄漏 / 驱动报错 | 交给将来的 `ResourceManager`；若让 renderer 管，务必在 `glfwTerminate()` 之前销毁 |

### 9.5 清理项

- `main.cpp` 里的 `vertices` / `indices`（旧三角形数据）、局部 `view` / `projection` / `aspect` / `OrthoProjectionMatrix` / `viewWidth` / `viewHeight` 都是死代码
- `main.cpp` 里的 `//#include "OpenGLRenderer.h"` 和 `//IRenderer* renderer = new OpenGLRenderer(...)` 两条死注释
- `CreateRenderer(800, 600)` 的硬编码数字应改为 `WINDOW_WIDTH` / `WINDOW_HEIGHT`
- `OpenGLRenderer::window` 是 public，建议改 private
- `Material` 的构造函数建议加 `explicit`
- `Resources/ResourceManager.h` 是空文件
- `目标渲染架构.md` 里关于 `Shader*` / `config.h` 万能头的描述已过时

### 9.6 下一步规划（详见 `src/目标渲染架构.md`）

按优先级：

1. **收口后端的"选择与装配"**：把 `config.h` 从 `Renderer.h` 摘走（N1/N3），工厂里换成 `#error`（N4），`main` 里加空指针检查 —— 这几条是"下一次踩坑"的直接来源
2. **修 R1 / R2**（blend 映射 + 越过状态缓存的接口），这是当前画面问题的直接原因
3. **摄像机输入通路**（C1~C4：恢复 `if (dragging)`、删掉自转、注册回调、处理 resize）
4. **`RenderQueue`**：排序 + 剔除（R3）
5. **`FrameData`**：把 view/projection/光源/清屏色每帧传一次（R4），顺便让 `IRenderer` 瘦身
6. **`Scene` 层**：`Transform` / `MeshFilter` / `MeshRenderer` / `SceneManager`
7. **`ResourceManager`**：Shader / Mesh / Texture 的加载与缓存（M4）
8. **`RenderTarget`（FBO）**：多 Pass 渲染、后处理、透明物体与场景交界的深度软化

---

## 十、本轮重构变更记录

### 目标

把"跟 OpenGL 严重耦合的 `Shader` / `Mesh`"拆成"抽象接口 + 具体实现"，并让应用层不再认识具体后端。

### 做了什么

| 项目 | 之前 | 现在 |
|---|---|---|
| Shader | `src/Shader.h/.cpp`（直接调 gl*，在 src 根目录） | `Render/IShader.h`（接口）+ `Render/OpenGL/OpenGLShader.*`（实现） |
| Mesh | `src/Mesh.h/.cpp`（同上） | `Render/IMesh.h`（接口）+ `Render/OpenGL/OpenGLMesh.*`（实现） |
| Material | 持有 `Shader*` → 间接依赖 glad | 持有 `IShader*` → 只依赖抽象接口 ✅ |
| RenderCommand | `Mesh*` | `IMesh*` ✅ |
| 资源创建 | `main` 里 `new Shader/Mesh` | `renderer->CreateShader()` / `CreateMesh()`（抽象工厂）✅ |
| 后端创建 | `main` 里 `new OpenGLRenderer` | `CreateRenderer()` 自由函数 + `RendererFactory.cpp` 装配 ✅ |
| 后端选择 | 无 | 编译期宏 `OPENGL_RENDERER` / `VULKAN_RENDERER` ✅ |
| `main.cpp` 的 include | `OpenGLShader.h` / `OpenGLMesh.h` / `OpenGLRenderer.h` | 只剩 `Camera.h` / `Renderer.h` / `<iostream>` ✅ |
| `config.h` | "万能头"，include 了一切 | 只剩一行后端选择宏 ✅ |
| `IRenderer::Render(Mesh*, Shader*, mat4&)` | 空实现的遗留接口 | 已删除 ✅ |
| `Clear()` 的深度 mask | 受上一帧 `depthWrite` 影响，深度清不干净 | 先 `glDepthMask(GL_TRUE)` 并同步缓存 ✅ |
| `RenderObject.h/.cpp` | 临时结构，已无人使用 | 已删除 ✅ |

### 现在的架构一句话总结

> **接口在中间，实现在下层，装配只有一处，应用层只认识接口。**
> `main.cpp` 里搜不到 `OpenGL` / `glad` / `glfw`（除了两条被注释掉的遗留写法：`//#include "OpenGLRenderer.h"` 和 `//IRenderer* renderer = new OpenGLRenderer(...)`）—— 这就是验收标准。

### 还没做完的

见 [9.3 架构与解耦（本轮的遗留）](#93-架构与解耦本轮的遗留)。
最关键的三条是 **N1（`Renderer.h` 里 include `config.h`）**、**N3（工厂对 config.h 的隐式依赖）**、**N4（`#else` 返回 nullptr 而没有 `#error`）** ——
它们共同构成"下次改宏时又会崩一次"的隐患。
