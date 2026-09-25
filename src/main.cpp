#include <iostream>

#include "Camera.h"
#include "Renderer.h"
#include "RenderQueue.h"
//#include "OpenGLRenderer.h"
// ============================ 编译期常量 ============================
// constexpr 是编译期常量，比 #define 更安全（有类型检查、有作用域）。
// 这里定义窗口的初始宽高。
constexpr int WINDOW_WIDTH = 800;
constexpr int WINDOW_HEIGHT = 600;

// ============================ 程序入口 ============================
int main(){
    // ======================================== Renderer ======================================================
    // 创建窗口以及设置OpenGL上下文
    //IRenderer* renderer = new OpenGLRenderer(WINDOW_WIDTH, WINDOW_HEIGHT);
    IRenderer* renderer = CreateRenderer(WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!renderer) { std::cerr << "创建渲染器失败：后端未编入？" << std::endl; return -1; }
    if (!renderer->Init()) {
        std::cout << "Failed to initialize renderer" << std::endl;
        return -1;
    }

    // ======================================== 摄像机 ======================================================
    Camera camera;
    camera.SetViewportSize(renderer->GetWindowSize());  // 设置摄像机窗口大小

    // ======================================== Shader ======================================================
    // 猴头
    std::string vsPath = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/basicvertex.glsl";
    std::string fsPath = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/basicfrag.glsl";
    IShader* shader = renderer->CreateShader(); //
    shader->BuildFromFiles(vsPath, fsPath);
    Material material(shader);
    material.renderState.depthTest = true;
    // 平面Shader-半透明物体
    std::string vsPath1 = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/groundNetVertex.glsl";
    std::string fsPath1 = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/groundNetFrag.glsl";
    IShader* planeShader = renderer->CreateShader();
    planeShader->BuildFromFiles(vsPath1, fsPath1);
    Material planeMaterial(planeShader);
    planeMaterial.renderState.blend = BlendMode::AlphaBlend;
    planeMaterial.renderState.depthTest = true;
    planeMaterial.renderState.depthWrite = false;


    // ======================================== 基础三角形网格 ======================================================
    //   坐标是 NDC（标准化设备坐标），范围 -1~1，屏幕中心是原点，所以这三个点正好围成居中的三角形。片段着色器
    // vec4(vertexColor,1.0) 会把三个角的颜色插值，你会看到一个红绿蓝渐变的三角形。
    float vertices[] = {
        // 位置 x     y     z        颜色 r   g   b
         0.0f,  0.5f, 0.0f,        1.0f, 0.0f, 0.0f,   // 上顶点  -> 红
        -0.5f, -0.5f, 0.0f,        0.0f, 1.0f, 0.0f,   // 左下    -> 绿
         0.5f, -0.5f, 0.0f,        0.0f, 0.0f, 1.0f,   // 右下    -> 蓝
    };
    // 索引
    unsigned int indices[] ={
        0,1,2
    };

    // ======================================== 加载 OBJ 模型 ======================================================
    // 猴头模型
    ObjMeshData objMeshData;
    const std::string objPath = std::string(PROJECT_SOURCE_DIR) + "/src/mesh/monkey.obj";
    if (!LoadObj(objPath, objMeshData)) {
        std::cerr << "加载 OBJ 失败: " << objPath << std::endl;
        renderer->WindowTerminate(); // 终止窗口
        return -1;
    }
    // 建立网格并上传到 GPU（直接把加载结果喂给 SetData）
    IMesh* mesh = renderer->CreateMesh();
    mesh->SetData(objMeshData);
    // 平面
    ObjMeshData objMeshData1;
    const std::string objPath1 = std::string(PROJECT_SOURCE_DIR) + "/src/mesh/plane.obj";
    if (!LoadObj(objPath1, objMeshData1)) {
        std::cerr << "加载 OBJ 失败: " << objPath1 << std::endl;
        renderer->WindowTerminate();  // 终止窗口
        return -1;
    }
    // 建立网格并上传到 GPU（直接把加载结果喂给 SetData）
    IMesh* plane = renderer->CreateMesh();
    plane->SetData(objMeshData1);


    // ======================================= MVP矩阵 ================================================================
    // ===== 2. View矩阵（摄像机） =====
    // glm::lookAt(摄像机位置, 看向的目标点, 上方向)
    glm::mat4 view = glm::lookAt(
        glm::vec3(4.0f, 2.0f, 5.0f),  // eye: 摄像机在 (0, 0, -2)
        glm::vec3(0.0f, 0.0f, 0.0f),   // center: 看向原点（物体在原点）
        glm::vec3(0.0f, 1.0f, 0.0f)    // up: Y轴向上
    );
    // ===== 3. Projection矩阵（投影） =====
    float aspect = (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT;
    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f),           // FOV: 45度
        aspect,                        // aspect: 宽高比
        0.1f,                          // near: 近裁剪面
        100.0f);                       // far: 远裁剪面
    // ===== 4. Projection矩阵（正交投影） =====
    // 假设你想让摄像机前方 20x20 的世界单位区域可见，并根据窗口宽高比调整宽度
    float viewHeight = 20.0f;
    float viewWidth = viewHeight * camera.aspectRatio;
    glm::mat4 OrthoProjectionMatrix = glm::ortho(
        -viewWidth / 2.0f,  // left
         viewWidth / 2.0f,  // right
        -viewHeight / 2.0f, // bottom
         viewHeight / 2.0f, // top
         0.1f,              // near (注意：正交投影中 near/far 也要合理设置，不能都为正但顺序反了)
         100.0f             // far
    );

    // ==================================== 渲染队列 ===================================
    RenderQueue renderQueueManager;
    renderQueueManager.Submit(RenderCommand(plane, &planeMaterial, Transform(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(100.0f, 1.0f, 100.0f))));
    renderQueueManager.Submit(RenderCommand(mesh, &material, Transform(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f))));
    // 获取摄像机数据
    CameraData renderingCameraData = camera.GetCameraData();
    // 渲染队列排序
    renderQueueManager.Sort(renderingCameraData.position);

    // ---------------------------------------------------------------
    // 【第 6 步】渲染主循环
    while (!renderer->WindowShouldClose()) {

        // 处理所有窗口事件（键盘输入、鼠标移动等）
        renderer->PollEvents();
        camera.mouseX += 1.0;
        camera.Update();
        renderingCameraData = camera.GetCameraData();

        // 渲染队列排序
        renderQueueManager.Sort(renderingCameraData.position);
        
        // 清空颜色缓冲（用 glClearColor 设置的颜色填充窗口）
        renderer->Clear();  // 清空颜色缓冲

        // 执行渲染命令
        renderer->ExecuteRenderCommands(renderQueueManager.Commands(), renderingCameraData);

        renderer->SwapBuffers(); // 交换前后缓冲区（把画好的内容显示到屏幕）
    }
    // 释放资源
    delete mesh;
    delete plane;
    delete shader;
    delete planeShader;
    delete renderer;

    return 0;
}
