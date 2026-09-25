#include "OpenGLRenderer.h"

OpenGLRenderer::OpenGLRenderer(const int width, const int height) : WINDOW_WIDTH(width), WINDOW_HEIGHT(height) {

}

OpenGLRenderer::~OpenGLRenderer() {
    // ---------------------------------------------------------------
    // 【第 7 步】清理并退出
    // glfwTerminate 释放 GLFW 占用的所有资源（窗口、上下文等）。
    // 返回 0 表示程序正常结束。
    // ---------------------------------------------------------------
    glfwTerminate();
}

glm::vec2 OpenGLRenderer::GetWindowSize() {
    return glm::vec2(WINDOW_WIDTH, WINDOW_HEIGHT);
}


bool OpenGLRenderer::Init() {
    return CreateWindow();
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
    // 注意: 你的着色器文件里写的是 "#version 460 core"，那需要 4.6 的上下文；
    //       当前只请求 4.6，若真去编译该着色器会因版本不匹配而失败。
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
    // glfwGetProcAddress 提供“按名字查函数地址”的能力给 GLAD 使用。
    // ---------------------------------------------------------------
    // GLAD 必须在 OpenGL 上下文创建之后初始化
    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        glfwTerminate();
        return false;
    }

    // 所有 OpenGL 调用必须在 GLAD 加载之后！
    // 设置清屏颜色（RGBA，取值范围 0.0~1.0）。
    // 这里是一种青绿色: R=0.2 G=0.3 B=0.3 A=1.0

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
void OpenGLRenderer::Clear() {
    glDepthMask(GL_TRUE);
    mRenderState.depthWrite = true;   // 改了 GL 状态，这里如果不重新设置开启，当绘制关闭了深度写入的透明物体后，深度缓存会失效，缓存必须同步（否则又会不一致）
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// 渲染队列
// 执行渲染命令
void OpenGLRenderer::ExecuteRenderCommands(const std::vector<RenderCommand>& RenderingCommandQueue,const CameraData& RenderingCameraData) {
    // 获取视图矩阵和投影矩阵
    glm::mat4 ViewMatrix = RenderingCameraData.viewMatrix;
    glm::mat4 ProjectionMatrix = RenderingCameraData.projectionMatrix;
    // 在这里后续添加排序相关逻辑


    glEnable(GL_MULTISAMPLE);  // 开启多重采样
    // BasePass
    for (auto& command : RenderingCommandQueue) {
        // 获取着色器
        // 对象用 . , 指针用 ->
        command.material->GetShader()->Use();         // 使用着色器程序

        // 设置渲染状态
        ApplyRenderState(command.material->renderState);

        // 根据变换参数设置模型矩阵
        // 其实这个部分应该是直接存储的，而不是在执行时反复计算？
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        modelMatrix = glm::translate(modelMatrix, command.transform.position);
        modelMatrix = glm::rotate(modelMatrix, command.transform.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
        modelMatrix = glm::rotate(modelMatrix, command.transform.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
        modelMatrix = glm::rotate(modelMatrix, command.transform.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
        modelMatrix = glm::scale(modelMatrix, command.transform.scale);

        // 设置 MVP 矩阵
        command.material->GetShader()->SetMatrix(modelMatrix, ViewMatrix, ProjectionMatrix);  // 设置 MVP 矩阵
        command.material->GetShader()->SetLight(glm::vec3(0.5f, 1.0f, 0.2f),glm::vec3(1.0f, 1.0f, 1.0f)); // 设置光源
        command.material->GetShader()->SetCamera(RenderingCameraData.position); // 设置相机位置
        command.mesh->Draw();                     // 绘制网格

    }
    //RenderCommandQueue.clear();
}

void OpenGLRenderer::ApplyRenderState(const RenderState& renderState) {

    // 深度测试
    if(renderState.depthTest != mRenderState.depthTest){
        if(renderState.depthTest){
            glEnable(GL_DEPTH_TEST);
        }else{
            glDisable(GL_DEPTH_TEST);
        }
        mRenderState.depthTest = renderState.depthTest;
    }
    // 深度写入
    if(renderState.depthWrite != mRenderState.depthWrite){
        if(renderState.depthWrite){
            glDepthMask(GL_TRUE);
        }else{
            glDepthMask(GL_FALSE);
        }
        mRenderState.depthWrite = renderState.depthWrite;
    }
    // 深度比较
    if(renderState.depthFunc != mRenderState.depthFunc){
        glDepthFunc(RenderStateToOpenGL(renderState.depthFunc));
        mRenderState.depthFunc = renderState.depthFunc;
    }
    // 剔除模式
    if(renderState.cullMode != mRenderState.cullMode){
        if(renderState.cullMode == CullMode::Off){
            glDisable(GL_CULL_FACE);
        }else{
            glEnable(GL_CULL_FACE);
            glCullFace(RenderStateToOpenGL(renderState.cullMode));
        }
        mRenderState.cullMode = renderState.cullMode;
    }
    // 混合模式
    if(renderState.blend != mRenderState.blend){
        if(renderState.blend == BlendMode::Opaque){
            glDisable(GL_BLEND);
        }else{
            glEnable(GL_BLEND);
            glBlendFunc(RenderStateToOpenGL(renderState.blend), GL_ONE_MINUS_SRC_ALPHA);
        }
        mRenderState.blend = renderState.blend;
    }

    
}

GLenum OpenGLRenderer::RenderStateToOpenGL(DepthFunc f) {
    switch (f) {
        case DepthFunc::Never:        return GL_NEVER;
        case DepthFunc::Less:         return GL_LESS;
        case DepthFunc::Equal:        return GL_EQUAL;
        case DepthFunc::LessEqual:    return GL_LEQUAL;
        case DepthFunc::Greater:      return GL_GREATER;
        case DepthFunc::NotEqual:     return GL_NOTEQUAL;
        case DepthFunc::GreaterEqual: return GL_GEQUAL;
        case DepthFunc::Always:       return GL_ALWAYS;
    }
    return GL_LESS;
}
GLenum OpenGLRenderer::RenderStateToOpenGL(CullMode m) {
    switch (m) {
        case CullMode::Off:   return GL_NONE;
        case CullMode::Front: return GL_FRONT;
        case CullMode::Back:  return GL_BACK;
    }
    return GL_BACK;
}
GLenum OpenGLRenderer::RenderStateToOpenGL(BlendMode m) {
    switch (m) {
        case BlendMode::Opaque:     return GL_ONE;
        case BlendMode::AlphaBlend: return GL_SRC_ALPHA;
        case BlendMode::Additive:   return GL_ONE;
        case BlendMode::Multiply:   return GL_ZERO;
    }
    return GL_ONE;
}

// 相关资源创建
IShader* OpenGLRenderer::CreateShader() {
    return new OpenGLShader();
}
IMesh* OpenGLRenderer::CreateMesh() {
    return new OpenGLMesh();
}

