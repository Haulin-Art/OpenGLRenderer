#include "config.h"
// config.h 里已经按顺序引入了:
//   <iostream>      控制台输入输出
//   <glad/glad.h>   OpenGL 函数加载器（必须放在 GLFW 之前）
//   <GLFW/glfw3.h>  跨平台窗口/输入库
//   <fstream>/<sstream>/<string>  文件与字符串工具
// 注意 glad 必须在 glfw 之前引入，因为 GLFW 会用到 glad 提供的 OpenGL 函数指针。

// ============================ 编译期常量 ============================
// constexpr 是编译期常量，比 #define 更安全（有类型检查、有作用域）。
// 这里定义窗口的初始宽高。
constexpr int WINDOW_WIDTH = 800;
constexpr int WINDOW_HEIGHT = 600;


// ============================ 程序入口 ============================
int main(){
    // ======================================== Renderer ======================================================
    // 创建窗口以及设置OpenGL上下文
    IRenderer* renderer = new OpenGLRenderer(WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!renderer->Init()) {
        std::cout << "Failed to initialize renderer" << std::endl;
        return -1;
    }

    // ======================================== Shader ======================================================
    std::string vsPath = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/basicvertex.glsl";
    std::string fsPath = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/basicfrag.glsl";
    Shader shader(vsPath, fsPath);


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
        glfwTerminate();
        return -1;
    }
    // 建立网格并上传到 GPU（直接把加载结果喂给 setData）
    Mesh mesh;
    mesh.setData(objMeshData);
    // 平面
    ObjMeshData objMeshData1;
    const std::string objPath1 = std::string(PROJECT_SOURCE_DIR) + "/src/mesh/plane.obj";
    if (!LoadObj(objPath1, objMeshData1)) {
        std::cerr << "加载 OBJ 失败: " << objPath1 << std::endl;
        glfwTerminate();
        return -1;
    }
    // 建立网格并上传到 GPU（直接把加载结果喂给 setData）
    Mesh plane;
    plane.setData(objMeshData1);


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


    // ==================================== 渲染队列 ===================================
    std::vector<RenderCommand> renderQueue;
    renderQueue.push_back(RenderCommand(&mesh, &shader, Transform(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f))));
    renderQueue.push_back(RenderCommand(&plane, &shader, Transform(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(3.0f, 3.0f, 3.0f))));


    // ---------------------------------------------------------------
    // 【第 6 步】渲染主循环
    while (!renderer->WindowShouldClose()) {
        // 开启深度测试
        renderer->EnableRendererFeature(BuiltInRendererFeatures::DepthTest);

        // 清空颜色缓冲（用 glClearColor 设置的颜色填充窗口）
        renderer->Clear();  // 清空颜色缓冲

        // 执行渲染命令
        renderer->ExecuteRenderCommands(renderQueue,view, projection);

        // 处理所有窗口事件（键盘输入、鼠标移动等）
        renderer->PollEvents();
        renderer->SwapBuffers(); // 交换前后缓冲区（把画好的内容显示到屏幕）
    }

    delete renderer;

    return 0;
}
