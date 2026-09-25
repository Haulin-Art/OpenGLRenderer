#include "OpenGLRenderer.h"

#include "Passes/ShadowPass.h"   // 具体 Pass 只在 .cpp 里 include（头文件保持瘦）
#include "Passes/GBufferPass.h"
#include "Passes/SSAOPass.h"
#include "Passes/SSAOBlurPass.h"
#include "Passes/ScreenShadowPass.h"       // 屏幕空间 PCSS
#include "Passes/ScreenShadowBlurPass.h"
#include "Passes/SSGIPass.h"               // 屏幕空间全局光照
#include "Passes/SSGIBlurPass.h"           // 给 SSGI 去噪
#include "Passes/BasePass.h"

#include <algorithm>   // std::stable_sort

OpenGLRenderer::OpenGLRenderer(const int width, const int height) : WINDOW_WIDTH(width), WINDOW_HEIGHT(height) {

}

OpenGLRenderer::~OpenGLRenderer() {
    // ---------------------------------------------------------------
    // 【第 7 步】清理并退出
    // ---------------------------------------------------------------
    // ★ 顺序很重要：先销毁所有 Pass（它们的 GPU 资源 —— FBO / 纹理 / shader ——
    //   都必须在 GL 上下文还活着的时候删），最后才 glfwTerminate() 销毁上下文。
    mPasses.clear();

    // glfwTerminate 释放 GLFW 占用的所有资源（窗口、上下文等）
    glfwTerminate();
}

glm::vec2 OpenGLRenderer::GetWindowSize() {
    // ★ 实时查询「framebuffer 像素尺寸」，不要缓存成成员变量：
    //   1) glViewport 要的是「像素」，不是逻辑窗口尺寸
    //      （显示缩放 != 100% 时，glfwGetWindowSize 和 glfwGetFramebufferSize 不相等）
    //   2) 一旦缓存，resize 之后拿到的就是过期值
    if (!window) return glm::vec2(0.0f);   // Init() 之前调用要保护
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    return glm::vec2(static_cast<float>(w), static_cast<float>(h));
}


bool OpenGLRenderer::Init() {
    if (!CreateWindow()) return false;

    // ============================================================
    // 装配渲染管线
    //   ① 建 Pass（顺序无所谓，下面会按 Stage 排）
    //   ② stable_sort：Stage 小的先跑；stable 保证同 Stage 的 Pass 不被重排
    //   ③ 逐个 Setup（建各自的 GPU 资源；失败就整体失败）
    // ============================================================
    mPasses.emplace_back(std::make_unique<ShadowPass>());
    mPasses.emplace_back(std::make_unique<GBufferPass>());
    mPasses.emplace_back(std::make_unique<SSAOPass>());
    mPasses.emplace_back(std::make_unique<SSAOBlurPass>());
    mPasses.emplace_back(std::make_unique<ScreenShadowPass>());       // 屏幕空间 PCSS
    mPasses.emplace_back(std::make_unique<ScreenShadowBlurPass>());   // 给 PCSS 去噪

    // ★ SSGI 要额外留一个【非拥有】指针出来（按 G 切换开关用）。
    //   必须在 move 进 vector 【之前】调 .get() —— move 之后局部的 unique_ptr 就是空的了。
    //   顺序上和 Stage(370) 无关：下面会按 Stage 排，对象地址不会变。
    {
        auto ssgi = std::make_unique<SSGIPass>();
        mSSGI = ssgi.get();
        mPasses.emplace_back(std::move(ssgi));
    }
    mPasses.emplace_back(std::make_unique<SSGIBlurPass>());           // 给 SSGI 去噪（紧跟其后）

    mPasses.emplace_back(std::make_unique<BasePass>());

    std::stable_sort(mPasses.begin(), mPasses.end(),
                     [](const std::unique_ptr<OpenGLRenderPass>& a,
                        const std::unique_ptr<OpenGLRenderPass>& b) {
                         return a->Stage() < b->Stage();
                     });

    for (auto& pass : mPasses) {
        if (!pass->Setup()) {
            std::cerr << "Pass Setup 失败，渲染管线初始化中止" << std::endl;
            return false;
        }
    }

    // ★ Setup() 之后再补一次尺寸。
    //   像 GBufferPass 这种"RT 是屏幕尺寸"的 Pass，Setup() 里拿不到窗口尺寸，
    //   必须靠 OnResize 才能建出 RT。在这里先喂一次，保证第一帧之前
    //   所有 Pass 的资源都一定是齐的（也省得 Execute 里到处判"RT 建好了吗"）。
    const glm::vec2 initSize = GetWindowSize();
    mLastFBWidth  = static_cast<int>(initSize.x);
    mLastFBHeight = static_cast<int>(initSize.y);
    for (auto& pass : mPasses) pass->OnResize(mLastFBWidth, mLastFBHeight);

    // 让"按 G 切换 SSGI"在启动时就可见（否则没人知道有这个开关）
    if (mSSGI) {
        std::cout << "[Renderer] SSGI 默认" << (mSSGI->Enabled() ? "开启" : "关闭")
                  << "（运行时按 G 切换）" << std::endl;
    }

    return true;
}

bool OpenGLRenderer::CreateWindow() {
    // ---------------------------------------------------------------
    // 【第 2 步】初始化 GLFW
    //
    // GLFW 负责窗口创建、OpenGL 上下文管理、键盘/鼠标输入等。
    // glfwInit() 返回 0 表示初始化失败（比如缺少系统支持）。
    // ---------------------------------------------------------------
    if (!glfwInit()) {
        std::cout << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    // ---------------------------------------------------------------
    // 【第 3 步】设置要请求的 OpenGL 上下文版本与模式
    //
    // 这些 hint 必须在 glfwCreateWindow 之前设置。
    // 这里请求 OpenGL 4.6 + Core Profile（核心模式，去掉已废弃的旧接口）。
    // ---------------------------------------------------------------
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);   // 主版本号 = 4
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);   // 次版本号 = 6
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);   // 核心模式

    // ---------------------------------------------------------------
    // 【第 4 步】创建窗口及其 OpenGL 上下文
    //
    // 参数依次为: 宽、高、窗口标题、监视器(NULL=窗口模式)、共享上下文(NULL=不共享)。
    // 返回 NULL 表示创建失败，此时先终止 GLFW 再退出。
    // ---------------------------------------------------------------
    window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Lynn Renderer", NULL, NULL);
    if (!window) {
        std::cout << "Failed to create window" << std::endl;
        glfwTerminate();
        return false;
    }
    // 把新建窗口的 OpenGL 上下文设为当前线程的“当前上下文”，
    // 之后所有 gl* 调用都会作用在这个上下文上。
    glfwMakeContextCurrent(window); // 设置当前上下文为新创建的窗口

    // ---------------------------------------------------------------
    // 【第 5 步】初始化 GLAD（加载 OpenGL 函数指针）
    //
    // Windows 自带的 OpenGL 只有 1.1，要用 3.3/4.x 的函数必须动态加载。
    // GLAD 必须在上面的“上下文创建之后”才能初始化，否则拿不到函数地址。
    // ---------------------------------------------------------------
    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        glfwTerminate();
        return false;
    }

    // 所有 OpenGL 调用必须在 GLAD 加载之后！
    // 设置清屏颜色
    OpenGLRenderer::SetClearColor();

    return true;
}
void* OpenGLRenderer::GetWindow() {
    return window;
}

bool OpenGLRenderer::WindowShouldClose() {
    return glfwWindowShouldClose((GLFWwindow*)window);
}

void OpenGLRenderer::SwapBuffers() {
    glfwSwapBuffers(window);
}

void OpenGLRenderer::PollEvents() {
    glfwPollEvents();

    // ---------------------------------------------------------------
    // 运行时开关：按 G 切换 SSGI
    //
    // ★ 用 mSSGIKeyHeld 做"边沿检测"，只在【按下的那一下】翻转。
    //   glfwGetKey 返回的是"现在有没有按住"，不是"这一帧刚按下"。
    //   所以直接写 `if (按住) 翻转` 是错的：按住 G 100ms ≈ 6 帧 → 连翻 6 次，
    //   松手之后开关状态看起来是随机的。
    // ---------------------------------------------------------------
    if (!mSSGI || !window) return;

    const bool gDown = (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS);
    if (gDown && !mSSGIKeyHeld) {
        mSSGI->SetEnabled(!mSSGI->Enabled());
        std::cout << "[SSGI] " << (mSSGI->Enabled() ? "开启" : "关闭")
                  << "（按 G 切换）" << std::endl;
    }
    mSSGIKeyHeld = gDown;
}


// 底层API内置渲染特性
// 将 IRenderer 当中的 BuiltInRendererFeatures 转换为 OpenGL 的 GLenum
GLenum OpenGLRenderer::ConvertBuiltInRendererFeaturesToGLenum(BuiltInRendererFeatures feature){
    switch (feature) {
        case BuiltInRendererFeatures::DepthTest:   return GL_DEPTH_TEST;
        case BuiltInRendererFeatures::Blend:       return GL_BLEND;
        case BuiltInRendererFeatures::CullFace:    return GL_CULL_FACE;
        case BuiltInRendererFeatures::StencilTest: return GL_STENCIL_TEST;
        case BuiltInRendererFeatures::Multisample: return GL_MULTISAMPLE;
        case BuiltInRendererFeatures::ScissorTest: return GL_SCISSOR_TEST;
    }
    return 0;
}
void OpenGLRenderer::EnableRendererFeature(BuiltInRendererFeatures feature) {
    glEnable(ConvertBuiltInRendererFeaturesToGLenum(feature));
}

void OpenGLRenderer::DisableRendererFeature(BuiltInRendererFeatures feature)  {
    glDisable(ConvertBuiltInRendererFeaturesToGLenum(feature));
}

void OpenGLRenderer::WindowTerminate() {
    glfwTerminate();
}


// 设置清屏颜色
void OpenGLRenderer::SetClearColor(const glm::vec4& color) {
    glClearColor(color.r, color.g, color.b, color.a);
}
// 清屏
// ★ 注意：glClear(GL_DEPTH_BUFFER_BIT) 受 glDepthMask 控制 —— 上一帧若设过
//   depthWrite=false（透明物体就会），这里就清不掉深度缓冲。
//   所以先把 mask 打回 TRUE，并同步缓存。
//   （将来如果引入 ClearPass，这段逻辑要原样搬过去。）
void OpenGLRenderer::Clear() {
    glDepthMask(GL_TRUE);
    mRenderState.depthWrite = true;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// 渲染队列
// 执行渲染命令
// ============================================================
// 现在这个方法只干三件事：
//   ① 把本帧的数据填进「帧上下文」
//   ② 窗口尺寸变了就通知各 Pass
//   ③ 按 Stage 顺序把 Pass 跑一遍
//
// 具体画什么、怎么画，全部在各自的 Pass 里 —— 加新 Pass 不用改这个方法。
// ============================================================
void OpenGLRenderer::ExecuteRenderCommands(const std::vector<RenderCommand>& RenderingCommandQueue,const CameraData& RenderingCameraData) {
    glEnable(GL_MULTISAMPLE);  // TODO(S8): 一次性设置，应该挪到 CreateWindow() 里去

    // ---- ① 填本帧的上下文 ----
    const glm::vec2 fbSize = GetWindowSize();

    OpenGLRenderContext ctx;
    ctx.commands         = &RenderingCommandQueue;          // 已排好序的渲染队列
    ctx.fbWidth          = static_cast<int>(fbSize.x);      // ★ 必须填：Pass 用它恢复视口
    ctx.fbHeight         = static_cast<int>(fbSize.y);
    ctx.viewMatrix       = RenderingCameraData.viewMatrix;
    ctx.projectionMatrix = RenderingCameraData.projectionMatrix;
    ctx.cameraPos        = RenderingCameraData.position;
    ctx.lightPos         = lightPos;
    ctx.lightColor       = lightColor;
    // ctx.shadowMapTex / ctx.lightSpaceMatrix 由 ShadowPass 在它的 Execute 里写入

    // ---- ② 窗口尺寸变化时通知各 Pass ----
    if (ctx.fbWidth != mLastFBWidth || ctx.fbHeight != mLastFBHeight) {
        mLastFBWidth  = ctx.fbWidth;
        mLastFBHeight = ctx.fbHeight;
        for (auto& pass : mPasses) pass->OnResize(ctx.fbWidth, ctx.fbHeight);
    }

    // ---- ③ 按顺序跑完所有 Pass ----
    for (auto& pass : mPasses) {
        pass->Execute(ctx);
    }
}

// 相关资源创建
IShader* OpenGLRenderer::CreateShader() {
    return new OpenGLShader();
}
IMesh* OpenGLRenderer::CreateMesh() {
    return new OpenGLMesh();
}
