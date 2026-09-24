# OpenGLRenderer

一个从零手写的 OpenGL 渲染引擎（学习项目），目标是把"散落在 main 里的 OpenGL 调用"逐步重构成一套分层的渲染架构。

**技术栈**：C++17 · OpenGL 4.6 Core Profile · GLFW · GLAD · GLM · tinyobjloader

**当前能跑出来的东西**：一个可旋转的轨道摄像机视角下，一个猴头模型（不透明）+ 一片半透明的地面网格。

---

## 目录

- [一、构建与运行](#一构建与运行)
- [二、目录结构](#二目录结构)
- [三、分层与依赖规则](#三分层与依赖规则)
- [四、文件清单](#四文件清单)
- [五、文件之间的关系](#五文件之间的关系)
- [六、关键概念](#六关键概念)
- [七、Shader 与顶点格式约定](#七shader-与顶点格式约定)
- [八、已知问题与 TODO](#八已知问题与-todo)

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

### 改代码时的两个坑

1. **新增 `.cpp` 必须写进 `CMakeLists.txt` 的 `add_executable`**，否则它不会被编译，调用处会在**链接阶段**报 `undefined reference`。
2. **新增子目录必须加进 `target_include_directories`**，否则该目录下 `#include "xxx.h"` 会报 `No such file or directory`。
3. **改完 `CMakeLists.txt` 要重新跑一次 `cmake -S . -B build`**（改 CMake 脚本不会自动重配置）。

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
    ├── main.cpp                      ✅ 程序入口 + 主循环
    ├── config.h                      🚧 "万能头"：集中 include（临时方案，日后要拆）
    │
    ├── Shader.h / Shader.cpp         ✅ 着色器程序封装（资源层）
    ├── Mesh.h   / Mesh.cpp           ✅ VAO/VBO/EBO 封装（资源层）
    ├── ObjLoader.h / ObjLoader.cpp   ✅ OBJ 解析 + 顶点展开
    ├── RenderObject.h                🚧 临时结构体（最终由 Scene 层取代）
    ├── RenderObject.cpp              ⬜ 空文件
    │
    ├── Render/                       🚧 渲染核心层（设计目标：与图形 API 无关）
    │   ├── Renderer.h                🚧 后端接口 IRenderer + BuiltInRendererFeatures
    │   ├── RenderCommand.h           ✅ Transform + RenderCommand
    │   ├── Material.h / Material.cpp ✅ 材质：Shader + RenderState
    │   ├── Camera/
    │   │   ├── Camera.h              ✅ CameraData + Camera（轨道相机）
    │   │   └── Camera.cpp
    │   └── OpenGL/                   ✅ 具体 API 实现（唯一允许出现 gl* / glfw* 的地方）
    │       ├── OpenGLRenderer.h
    │       └── OpenGLRenderer.cpp
    │
    ├── Resources/                    ⬜ 资源管理
    │   └── ResourceManager.h         ⬜ 空文件（规划：加载/缓存 Shader、Mesh、Texture）
    │
    ├── mesh/
    │   ├── monkey.obj                ✅ 猴头模型（Suzanne）
    │   └── plane.obj                 ✅ 单位平面（4 顶点 2 三角形）
    │
    ├── shaders/
    │   ├── basicvertex.glsl          ✅ 猴头用的顶点着色器
    │   ├── basicfrag.glsl            ✅ 猴头用的片段着色器（简单兰伯特光照）
    │   ├── groundNetVertex.glsl      ✅ 地面网格用的顶点着色器
    │   └── groundNetFrag.glsl        ✅ 地面网格用的片段着色器（fwidth 抗锯齿网格）
    │
    ├── 目标渲染架构.md                ✅ 架构规划文档
    └── Core/
        └── 想法.md                   ✅ 随手记的疑问（沟通用）
```

**不属于本项目的目录**：`build/`（CMake 生成物，已在 `.gitignore` 里）。

---

## 三、分层与依赖规则

```
        main.cpp                  ← 主循环：调度更新、收集渲染命令、提交给后端
            │
            ├── Camera            ← 逻辑层：产出 View / Projection 矩阵
            │
            ├── Render/           ← 渲染核心层（设计目标：与图形 API 无关）
            │     Renderer.h        （后端接口）
            │     RenderCommand.h   （渲染命令）
            │     Material.h        （材质 + 渲染状态）
            │
            └── Render/OpenGL/    ← 具体 API 实现层
                  OpenGLRenderer    （唯一认识 OpenGL / GLFW 的地方）
                        │
              渲染资源 ──┴── Shader / Mesh / ObjLoader
```

### 三条硬规则

| 规则 | 说明 |
|---|---|
| ① **只有 `Render/OpenGL/` 里可以出现 `gl*` / `glfw*`** | 其它层一律不碰图形 API |
| ② **Camera 不知道窗口，也不知道渲染后端** | `aspect` 由上层喂进来（`Camera::SetViewportSize`） |
| ③ **Renderer 不知道 Camera / Scene** | 它只接收「一份帧数据 + 一串渲染命令」 |

> **判断一个设计对不对，问一句**：*"如果明天换成 DirectX 后端，哪些文件要改？"*
> 正确答案是：**只有 `Render/OpenGL/` 那一层**。

### 现实与目标的差距（重要）

规则 ① 目前**还没完全做到**：`Shader.h` / `Mesh.h` 里直接调用了 `gl*`，而且它们还在 `src/` 根目录。
`Render/Material.h` 因为 `#include "Shader.h"`，也间接把 `<glad/glad.h>` 拉进了"与 API 无关"的层。

目标形态是拆成"抽象接口 + 具体实现"：

```
Render/Shader.h          抽象接口（只有纯虚函数）
Render/OpenGL/GLShader.h 具体实现（里面才是 gl*）
Render/Mesh.h            抽象接口
Render/OpenGL/GLMesh.h   具体实现
```

也就是把已经做对的 `IRenderer` / `OpenGLRenderer` 这一对，复制到 Shader 和 Mesh 上。

---

## 四、文件清单

### 4.1 入口与工具

---

#### `src/config.h` 🚧

**职责**：集中 include 的"万能头"。所有源文件只要 `#include "config.h"` 就能拿到全部常用类型。

```cpp
#include <iostream>        // 控制台输出
#include <glad/glad.h>     // ★ 必须在 glfw 之前（GLFW 要用 glad 提供的函数指针）
#include <GLFW/glfw3.h>    // 窗口 / 输入
#include <fstream> / <sstream> / <string>
#include "Shader.h"        // 着色器
#include "Mesh.h"          // 网格
#include "Camera.h"        // 摄像机
#include "Renderer.h"      // 渲染后端接口
#include "OpenGLRenderer.h"// 具体后端
#include "ObjLoader.h"     // OBJ 加载
```

**问题**：这是临时方案。它让"谁依赖谁"完全不可见，还会把 GL/GLFW 头文件传染给每一个包含它的文件。日后应该拆掉，让每个文件只包含自己真正需要的头。

---

#### `src/main.cpp` ✅

**职责**：程序入口 + 渲染主循环。目前承担了太多职责（将来会被 `Application` / `Scene` 层接管）。

**流程**：

```
1. new OpenGLRenderer(WIDTH, HEIGHT)  →  IRenderer*
   renderer->Init()                   // 建窗口 + 上下文 + 加载 GLAD

2. Camera camera;
   camera.SetViewportSize(renderer->GetWindowSize());

3. Shader shader(vs, fs);              // 猴头：不透明
   Material material(&shader);
   material.renderState.depthTest = true;

   Shader planeShader(vs2, fs2);       // 地面：半透明
   Material planeMaterial(&planeShader);
   planeMaterial.renderState.blend      = BlendMode::AlphaBlend;
   planeMaterial.renderState.depthTest  = true;    // 要测深度
   planeMaterial.renderState.depthWrite = false;   // 但不写深度

4. LoadObj(...) → Mesh mesh / plane     // 上传到 GPU

5. 组装渲染队列：
   std::vector<RenderCommand> renderQueue;
   renderQueue.push_back({ &mesh,  &material,      Transform(...) });
   renderQueue.push_back({ &plane, &planeMaterial, Transform(...) });

6. 主循环：
   while (!renderer->WindowShouldClose()) {
       renderer->PollEvents();                        // 处理输入
       camera.mouseX += 1.0;                          // ← 调试用：驱动相机自转
       camera.Update();                               // 算 View / Projection
       renderer->Clear();                             // 清颜色 + 深度
       renderer->ExecuteRenderCommands(renderQueue, camera.GetCameraData());
       renderer->SwapBuffers();                       // 呈现
   }

7. delete renderer;
```

**依赖**：`config.h`（进而间接依赖几乎所有东西）
**被谁使用**：无（程序入口）

**已知问题**：
- `camera.mouseX += 1.0;` 是调试代码，导致相机每帧转 0.25°
- 大量死代码：`vertices` / `indices`（旧三角形数据）、局部 `view` / `projection` / `aspect` / `OrthoProjectionMatrix` 都不再被使用
- `new` 出来的 `renderer` 在两条提前 `return -1` 的路径上会泄漏（建议改用栈对象或智能指针）

---

### 4.2 渲染资源层

---

#### `src/Shader.h` / `src/Shader.cpp` ✅

**职责**：封装一个着色器程序（program）。**属于资源层**，只负责"程序本身"，不关心"怎么用它"。

**接口**：

| 成员 | 说明 |
|---|---|
| `unsigned int ID` | **public** 的 OpenGL program 句柄 |
| `Shader(vsPath, fsPath)` | 构造时立刻读文件 → 编译 → 链接 |
| `~Shader()` | `glDeleteProgram(ID)` |
| `Use()` | `glUseProgram(ID)` |
| `SetMatrix(model, view, proj)` | 上传 3 个 mat4 uniform |
| `SetLight(pos, color)` | 上传 `mainLightPos` / `mainLightColor` |
| `SetCamera(cameraPos)` | 上传 `CameraPos`（只有 groundNet 的 shader 用得到） |

**私有辅助**（外部不可见）：

| 成员 | 说明 |
|---|---|
| `buildFromFiles(vs, fs)` | 完整流程：读文件 → 编译 → 链接 → 检查日志 |
| `readShaderFile(path)` | 读文件到 `std::string`（`ifstream` + `stringstream`） |
| `compileShader(type, src)` | 编译单个 shader，失败时打印 `GL_COMPILE_STATUS` 日志 |

**依赖**：`<glad/glad.h>`、`<glm/glm.hpp>`、`<glm/gtc/type_ptr.hpp>`（`glm::value_ptr`）、文件流
**被谁使用**：`Material`（通过指针持有）、`OpenGLRenderer`（调用 `Use` / `Set*`）

**已知问题**：
1. `ID` 是 public —— 外部可以绕过封装直接 `glUseProgram(shader->ID)`
2. **每次 `SetMatrix` / `SetLight` / `SetCamera` 都调用 `glGetUniformLocation`**（字符串查找 + 驱动调用）。location 从链接成功那一刻就不会再变，应该查一次缓存起来。100 个物体 × 5 个 uniform = 每帧 500 次无谓查找
3. 构造函数里做 GL 操作 → 必须在上下文创建之后构造；且构造失败无法上报（`buildFromFiles` 的返回值被丢弃）
4. 它直接调用 `gl*`，却放在"与 API 无关"的 `Render/` 层之外的目标位置上

---

#### `src/Mesh.h` / `src/Mesh.cpp` ✅

**职责**：封装一份 GPU 网格数据（VAO + VBO + EBO）。

**接口**：

| 成员 | 说明 |
|---|---|
| `Mesh()` | 三个句柄初始化为 0 |
| `~Mesh()` | `glDeleteVertexArrays` + `glDeleteBuffers` |
| `setData(const float* vertices, int vertexCount, const unsigned int* indices, int indexCount)` | 重载 1：直接给裸数组 |
| `setData(const ObjMeshData&)` | 重载 2：喂 `ObjLoader` 的结果（main 用的是这个） |
| `draw() const` | `glBindVertexArray` + `glDrawElements(GL_TRIANGLES, mIndexCount, GL_UNSIGNED_INT, 0)` |

**私有**：`VAO` / `VBO` / `EBO` / `mIndexCount` / `setDataInternal(...)`

**顶点属性布局**（由 `setDataInternal` 设定，**着色器必须对得上**）：

| location | 属性 | 分量 | 偏移 | 字节 |
|---|---|---|---|---|
| 0 | 位置（aPos） | 3 × float | 0 | 12 |
| 1 | 法线（aNormal） | 3 × float | 12 | 12 |
| 2 | UV（aTexCoor） | 2 × float | 24 | 8 |
| — | **步长（stride）** | — | — | **32** |

**依赖**：`<glad/glad.h>`、`ObjLoader.h`
**被谁使用**：`RenderCommand`（通过 `Mesh*` 指针引用）、`OpenGLRenderer`

> 注：`Mesh.cpp` 里有几处注释还写着"每个顶点 6 个 float / stride 24"，那是早期三角形数据的遗留说法，
> 实际布局已经是 **8 个 float / stride 32**（位置 + 法线 + UV）。

---

#### `src/ObjLoader.h` / `src/ObjLoader.cpp` ✅

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
**被谁使用**：`main.cpp`（加载 monkey.obj / plane.obj）、`Mesh.h`（类型）

> 加载结果里如果某个顶点没有法线或 UV，对应分量填 0。

---

#### `src/RenderObject.h` 🚧 / `src/RenderObject.cpp` ⬜（空）

```cpp
struct RenderObject {
    Mesh*     mesh;
    Shader*   shader;      // ← 注意：还是 Shader*，没有升级成 Material*
    glm::mat4 transform;   // ← 直接存 mat4，和 RenderCommand 存 Transform 不一致
};
```

**职责**：早期版本的"渲染物体"结构体。
**现状**：`main.cpp` 已经改用 `RenderCommand`，这个结构**不再被使用**，等 Scene 层做出来后正式删掉。

---

### 4.3 Render 层（渲染核心，设计目标：与 API 无关）

---

#### `src/Render/Renderer.h` 🚧

**职责**：定义渲染后端的**抽象接口** `IRenderer`，以及一组内置渲染特性枚举。

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
    virtual void* GetWindow() = 0;                  // 拿原生窗口句柄（void* 以避免暴露 GLFW）
    virtual void  SetClearColor(const glm::vec4& = glm::vec4(0.2f,0.3f,0.3f,1.0f)) = 0;
    virtual void  Clear() = 0;                      // 清颜色 + 深度

    // ---- 帧 ----
    virtual bool WindowShouldClose() = 0;
    virtual void PollEvents() = 0;
    virtual void SwapBuffers() = 0;

    // ---- 渲染特性开关（底层逃生舱）----
    virtual void EnableRendererFeature(BuiltInRendererFeatures) = 0;
    virtual void DisableRendererFeature(BuiltInRendererFeatures) = 0;

    // ---- 渲染命令 ----
    virtual void ExecuteRenderCommands(const std::vector<RenderCommand>& cmds,
                                       const CameraData& cameraData) = 0;

    // ---- 遗留，未被实现 ----
    virtual void Render(Mesh*, Shader*, glm::mat4& transform) = 0;
};
```

**依赖**：`RenderCommand.h`（→ `Mesh.h` / `Shader.h` / `Material.h`）、`Camera.h`（为了 `CameraData`）
**被谁实现**：`OpenGLRenderer`
**被谁使用**：`main.cpp`

**问题**：
- `Render(Mesh*, Shader*, mat4&)` 是早期的遗留接口，`OpenGLRenderer` 里是**空实现**，应该删掉
- `EnableRendererFeature` / `DisableRendererFeature` 直接调 `glEnable` / `glDisable`，**会绕过状态缓存**（见 [六、关键概念](#六关键概念)），是个定时炸弹
- 接口名称不统一：`GetWindowSize()` vs `Camera` 那边的 `SetViewportSize()`；`CameraData` 的参数名在声明和定义里还不一致

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
    Mesh*     mesh;         // 画什么几何
    Material* material;     // 用什么材质（= Shader + 渲染状态）
    Transform transform;    // 放在哪里
};
```

**设计要点**：
- 只存 **Transform**，不存算好的 `mat4` —— 避免同一个 model 变换出现两个真相源
- 渲染时按 **T → R → S** 的顺序后乘（GLM 的 `translate/rotate/scale` 都是后乘）

**依赖**：`Mesh.h`、`Shader.h`、`Material.h`、`<glm/glm.hpp>`
**被谁使用**：`Renderer.h`（接口参数）、`main.cpp`（组装队列）、`OpenGLRenderer`（执行）

---

#### `src/Render/Material.h` / `src/Render/Material.cpp` ✅

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

    // 排序依据：把状态排在一起，减少状态切换
    bool operator<(const RenderState& o) const;
};

// ---- 材质 ----
class Material {
public:
    Material(Shader* shader);
    void     SetShader(Shader*);
    Shader*  GetShader() const;
    RenderState renderState;    // 公开数据，上层直接赋值即可配置

private:
    Shader* m_Shader = nullptr;  // ★ 不拥有所有权，只引用
};
```

**⚠️ 关于 `RenderState` 的默认值（重要）**

默认值被**刻意**设成了 **OpenGL 的真实初始状态**：`depthTest = false`、`cullMode = Off`、`blend = Opaque`。
这样 `OpenGLRenderer` 里的状态缓存（`mRenderState`）从一开就"说的是真话"。

**代价**：`Material` 的默认状态是**"不测深度、不剔面"**。所以**新建材质时一定要显式写全状态**，例如：

```cpp
material.renderState.depthTest = true;    // 不写的话，默认是不测深度的！
```

**依赖**：`Shader.h`（因此间接依赖 glad/GLFW —— 这是分层上的一处泄漏）
**被谁使用**：`RenderCommand`、`OpenGLRenderer`

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

### 4.4 Render/OpenGL 层（唯一碰图形 API 的地方）

---

#### `src/Render/OpenGL/OpenGLRenderer.h` / `OpenGLRenderer.cpp` ✅

**职责**：`IRenderer` 的 OpenGL 实现。**整个项目里唯一允许出现 `gl*` / `glfw*` 的地方。**

**公开成员**：

| 成员 | 说明 |
|---|---|
| `GLFWwindow* window` | public 的窗口句柄（未来应改 private） |
| `Init()` | → `CreateWindow()` |
| `GetWindowSize()` | 返回 `glm::vec2(WINDOW_WIDTH, WINDOW_HEIGHT)`（**固定值，不随窗口 resize 变化**） |
| `GetWindow()` | 返回 `void*` 窗口句柄 |
| `WindowShouldClose()` / `PollEvents()` / `SwapBuffers()` | GLFW 转发 |
| `SetClearColor(color)` | `glClearColor` |
| `Clear()` | `glClear(GL_COLOR_BUFFER_BIT \| GL_DEPTH_BUFFER_BIT)` |
| `EnableRendererFeature` / `DisableRendererFeature` | `glEnable` / `glDisable`（⚠️ 绕过状态缓存） |
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
for (每条命令 command) {
    command.material->GetShader()->Use();              // 1. 启用着色器程序
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
    command.mesh->draw();
}
```

**依赖**：`Renderer.h`、`<glad/glad.h>`、`<GLFW/glfw3.h>`
**被谁使用**：`main.cpp`（以 `IRenderer*` 的形式）

---

### 4.5 待做 / 占位

| 文件 | 状态 | 规划职责 |
|---|---|---|
| `src/Resources/ResourceManager.h` | ⬜ 空文件 | 单例，负责 Shader / Mesh / Texture 的加载与缓存；**Shader 的生命周期应该由它负责**（因为 `Material` 只引用不拥有） |
| `src/RenderObject.cpp` | ⬜ 空文件 | `RenderObject` 已经没有成员函数，这个 .cpp 可以直接删 |

---

### 4.6 文档

| 文件 | 说明 |
|---|---|
| `src/目标渲染架构.md` | 架构规划：分层与依赖规则、矩阵归属、主循环顺序、接口草图、目录现状（✅/🚧/⬜）、下一步优先级 |
| `src/Core/想法.md` | 随手记的疑问，作为沟通渠道用（例如"是不是该有个 Manager？""矩阵该放哪里？"） |

---

## 五、文件之间的关系

### 5.1 Include（编译期依赖）

```
config.h
  ├─→ <glad/glad.h>  <GLFW/glfw3.h>  <iostream> <fstream> <sstream> <string>
  ├─→ Shader.h ────→ glad / glm
  ├─→ Mesh.h ──────→ glad / ObjLoader.h
  ├─→ Camera.h ────→ glm（唯一一个干净的头）
  ├─→ Renderer.h ──→ RenderCommand.h ──→ Mesh.h / Shader.h / Material.h ──→ Shader.h
  │                   └─→ Camera.h
  ├─→ OpenGLRenderer.h ──→ Renderer.h
  └─→ ObjLoader.h ─→ <string> <vector>

main.cpp ──→ config.h（间接拿到上面全部）

OpenGLRenderer.cpp ──→ OpenGLRenderer.h
Material.cpp       ──→ Material.h
Camera.cpp         ──→ Camera.h
```

**读法**：箭头方向 = "包含"。你会看到 `config.h` 一点连出一大片 —— 这就是它被叫做"万能头"的原因，也是它要被拆掉的原因。

### 5.2 对象拥有关系（运行期）

```
main()
 ├── IRenderer* renderer ──(new/delete)──> OpenGLRenderer  ── 拥有 → GLFWwindow
 │                                          └── 内嵌 mRenderState（状态缓存）
 │
 ├── Camera  camera          （栈对象，纯数据 + 几个方法）
 │
 ├── Shader  shader          （栈对象，拥有 GL program）
 ├── Shader  planeShader     （栈对象）
 │      ▲         ▲
 │      │ 引用    │ 引用        ← Material 只引用，不拥有！
 ├── Material material(&shader)
 ├── Material planeMaterial(&planeShader)
 │
 ├── Mesh mesh / plane       （栈对象，拥有 VAO/VBO/EBO）
 │
 └── std::vector<RenderCommand> renderQueue
        └── 每条命令 { Mesh*, Material*, Transform }   ← 全是指针，不拥有
```

> **所有权规则**：`Shader`、`Mesh` 自己管自己的 GPU 资源（构造/析构）；
> `Material` 和 `RenderCommand` **只引用不拥有**，所以它们的生命周期必须短于被引用的对象。
> 将来 Shader / Mesh 会交给 `ResourceManager` 统一管理。

### 5.3 一帧的数据流

```
   main 主循环
       │
       ├─① renderer->PollEvents()          → GLFW 处理输入（回调会写 camera 的鼠标状态）
       │
       ├─② camera.Update()                → 球坐标换算成 View / Projection，存进 mCameraData
       │
       ├─③ renderer->Clear()              → glClear(颜色 | 深度)
       │
       └─④ renderer->ExecuteRenderCommands(renderQueue, camera.GetCameraData())
              │
              │   renderQueue: [ {mesh, material, transform}, {plane, planeMaterial, transform} ]
              │   cameraData : { position, viewMatrix, projectionMatrix }
              │
              ▼
        OpenGLRenderer::ExecuteRenderCommands
              │
              └─（逐条命令）
                    ├─ material->GetShader()->Use()          → glUseProgram
                    ├─ ApplyRenderState(material->renderState)
                    │      └─ 与 mRenderState 比较，只把"变化的部分"翻译成
                    │         glEnable/glDisable/glDepthMask/glDepthFunc/glCullFace/glBlendFunc
                    ├─ 算 Model 矩阵（T→R→S）
                    ├─ shader->SetMatrix / SetLight / SetCamera  → glUniform*
                    └─ mesh->draw()                          → glBindVertexArray + glDrawElements
              │
              ▼
        renderer->SwapBuffers()            → glfwSwapBuffers（呈现）
```

---

## 六、关键概念

### 6.1 渲染命令（RenderCommand）

一条命令 = **"画什么（mesh）+ 用什么（material）+ 放在哪（transform）"**。

它的意义是**把"收集"和"执行"解耦**：

- `main` / 将来的 Scene 层负责**收集**（遍历场景、剔除、排序）→ 产出一串命令
- 渲染后端负责**执行**（逐条翻译成 GL 调用）

这样后端完全不需要知道"场景里有什么"，换后端时收集逻辑一行都不用改。

### 6.2 材质（Material）= Shader + 渲染状态

为什么要单独有个 `Material`，而不是让 `RenderCommand` 直接指向 `Shader`？

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

### 6.3 渲染状态缓存（最容易出 bug 的地方）

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

> **另一个经典陷阱**：`glClear(GL_DEPTH_BUFFER_BIT)` **受 `glDepthMask` 控制**！
> 如果上一帧最后把 `depthWrite` 设成 `false`，下一帧的 `Clear()` 就**清不掉深度缓冲**。
> 因为 `Clear()` 在状态设置之前执行，用到的是"上一帧遗留"的 mask。修法是在 `Clear()` 里先
> `glDepthMask(GL_TRUE)` 并同步缓存。

### 6.4 矩阵归属

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

### 6.5 渲染循环的顺序（为什么这么排）

```
① PollEvents()          ← 先处理输入。鼠标回调是它触发的，反了的话相机晚一帧
② camera.Update()       ← 再更新逻辑（读回调刚写进去的鼠标状态）
③ Clear()               ← 清屏（注意 depth mask 的坑，见 6.3）
④ ExecuteRenderCommands ← 收集好的命令 + 相机数据
⑤ SwapBuffers()         ← 呈现
```

**原则**：`Update` 属于**逻辑层**，只改数据、**不做任何 GL 调用**；渲染的事全交给后端。

### 6.6 透明物体

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

## 七、Shader 与顶点格式约定

### 7.1 顶点属性（由 `Mesh` 设定，着色器必须一致）

```glsl
layout (location = 0) in vec3 aPos;      // 位置，偏移 0 字节
layout (location = 1) in vec3 aNormal;   // 法线，偏移 12 字节
layout (location = 2) in vec2 aTexCoor;  // UV，  偏移 24 字节
// 步长（stride）= 32 字节
```

### 7.2 Uniform 约定

| uniform | 类型 | basicvertex / basicfrag | groundNetVertex / groundNetFrag | 由谁设置 |
|---|---|---|---|---|
| `ModelMatrix` | mat4 | ✔ | ✔ | `Shader::SetMatrix` |
| `ViewMatrix` | mat4 | ✔ | ✔ | `Shader::SetMatrix` |
| `ProjectionMatrix` | mat4 | ✔ | ✔ | `Shader::SetMatrix` |
| `mainLightPos` | vec3 | ✔ | ✔ | `Shader::SetLight` |
| `mainLightColor` | vec3 | ✔ | ✔ | `Shader::SetLight` |
| `CameraPos` | vec3 | ✖ | ✔ | `Shader::SetCamera` |

> 对着没有声明该 uniform 的 shader 调 `SetCamera` 是无害的：
> `glGetUniformLocation` 会返回 -1，而 `glUniform*` 在 location 为 -1 时**被规范要求忽略**。

### 7.3 两个着色器对

**`basicvertex.glsl` / `basicfrag.glsl`** —— 猴头用
- 顶点：`gl_Position = P * V * M * vec4(aPos, 1.0)`，把法线直接传给片元
- 片元：半兰伯特光照 `lambert = dot(N, normalize(lightPos)) * 0.5 + 0.5`，输出 **alpha = 1.0（不透明）**

**`groundNetVertex.glsl` / `groundNetFrag.glsl`** —— 地面网格用
- 顶点：除了 `gl_Position`，还输出 `vertexPos`（模型空间位置）和 `posWS`（世界空间位置）
- 片元：用 `fwidth` 做**屏幕空间抗锯齿**的无限网格：
  - 小网格 / 大网格 / 红色轴线（x 为整数）/ 蓝色轴线（z 为整数）
  - 用 `distance(CameraPos, posWS)` 做**距离淡出**（远处不画，省性能也好看）
  - 输出 `fragColor = vec4(netColor, alpha)`，其中 `alpha = 小网格的遮罩` → 大部分像素接近全透明

---

## 八、已知问题与 TODO

### 8.1 渲染状态（优先级最高）

| # | 问题 | 后果 | 建议 |
|---|---|---|---|
| 1 | `Clear()` 里的 `glClear(GL_DEPTH_BUFFER_BIT)` 受 `glDepthMask` 影响 | 上一帧若设了 `depthWrite=false`，本帧**深度缓冲清不掉** → 遮挡全乱、模型"被撕碎" | 在 `Clear()` 开头 `glDepthMask(GL_TRUE)` 并同步 `mRenderState.depthWrite` |
| 2 | `RenderStateToOpenGL(BlendMode)` 返回单个值，却用来喂 `glBlendFunc` 的两个参数 | `AlphaBlend` 恰好正确；**`Additive` / `Multiply` 是错的** | 改成 `switch`，一个 case 里同时设 `glBlendFunc(src, dst)` |
| 3 | `EnableRendererFeature` / `DisableRendererFeature` 直接 `glEnable`/`glDisable` | **绕过状态缓存** → 缓存失真 → 之后所有状态判断都错 | 删掉 main 里的调用；或让它们同步更新 `mRenderState` |
| 4 | 渲染队列没有排序 | 透明物体必须最后画，现在只是"恰好"顺序对 | 收集阶段按 `RenderState::operator<` 排序，透明物体排最后 |
| 5 | 光源参数写死在 `ExecuteRenderCommands` 里 | 每个物体都重复上传同一份光照；无法配置 | 挪进 `CameraData` 同级的"帧数据"结构，每帧传一次 |

### 8.2 摄像机与输入

| # | 问题 | 建议 |
|---|---|---|
| 6 | `Camera.cpp` 里 `if (dragging)` 被注释掉 | 恢复它，否则"不按左键光移动鼠标"也会旋转 |
| 7 | `main.cpp` 里 `camera.mouseX += 1.0;` | 删掉（这是让相机自转的调试代码） |
| 8 | 没有任何鼠标回调注册 | 需要 `glfwSetCursorPosCallback` / `glfwSetMouseButtonCallback` → 转发给 `Camera::BeginDrag/EndDrag`。**建议后端只暴露"回调注册"接口，由上层转发**，不要在 `OpenGLRenderer` 里直接认识 `Camera` |
| 9 | 没有处理窗口 resize | 注册 `glfwSetFramebufferSizeCallback`，回调里 `glViewport` + 更新相机 aspect |
| 10 | `GetWindowSize()` 返回的是构造时的固定值 | 改成 `glfwGetFramebufferSize(window, ...)`，否则拉伸窗口画面会变形 |

### 8.3 架构

| # | 问题 | 建议 |
|---|---|---|
| 11 | `Shader.h` / `Mesh.h` 直接调 `gl*`，还在 `src/` 根目录 | 拆成 `Render/Shader.h`（抽象接口）+ `Render/OpenGL/GLShader.h`（实现），Mesh 同理 |
| 12 | `Material.h` 因为 include `Shader.h` 而间接依赖 GL | 等 11 做完自然解决 |
| 13 | `config.h` 是万能头 | 拆掉，让每个文件只 include 自己需要的头 |
| 14 | `IRenderer::Render(Mesh*, Shader*, mat4&)` 空实现 | 删掉 |
| 15 | `Material.h` 里 `Shader* GetShader()` 不拥有所有权，但没有注释说明 | 补注释；将来由 `ResourceManager` 统一管理 Shader 生命周期 |
| 16 | `Shader` 每次都 `glGetUniformLocation` | 链接成功后查一次并缓存 location |

### 8.4 清理项

- `main.cpp` 里的 `vertices` / `indices`（旧三角形数据）、局部 `view` / `projection` / `aspect` / `OrthoProjectionMatrix` 都是死代码
- `RenderObject.h` 已不被使用；`RenderObject.cpp`、`Resources/ResourceManager.h` 是空文件
- `OpenGLRenderer::window` 是 public，建议改 private
- `Material` 的构造函数建议加 `explicit`

### 8.5 下一步规划（详见 `src/目标渲染架构.md`）

1. 修 `Clear()` 的深度 mask 问题 + blend 映射（当前画面问题的直接原因）
2. 摄像机输入通路（回调注册 + 转发）+ 窗口 resize
3. `RenderQueue`：排序 + 剔除
4. `Scene` 层：`Transform` / `MeshFilter` / `MeshRenderer` / `SceneManager`
5. `ResourceManager`：Shader / Mesh / Texture 的加载与缓存
6. `RenderTarget`（FBO）：多 Pass 渲染、后处理、透明物体与场景交界的深度软化
