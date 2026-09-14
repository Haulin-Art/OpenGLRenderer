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
    // ---------------------------------------------------------------
    // 【第 1 步】读取顶点着色器文件并打印内容
    //
    // 说明: 这里目前只是把着色器当普通文本读出来并打印到控制台，
    //       用于验证“文件路径是否正确、能否读到内容”。
    //       它还没有把着色器交给 OpenGL 编译/链接，所以现在这段
    //       shader 并没有真正作用到渲染上。
    // ---------------------------------------------------------------
    std::ifstream file;   // ifstream = input file stream，输入文件流对象
    std::string line;     // 用来暂存每次读到的一行文本

    // PROJECT_SOURCE_DIR 是一个宏，由 CMake 通过 target_compile_definitions 注入，
    // 内容就是项目根目录的绝对路径（例如 D:/Document/GitHub/OpenGLRenderer）。
    // 用绝对路径是为了避免“程序运行时工作目录不同”导致找不到文件。
    // std::string(PROJECT_SOURCE_DIR) 是把 C 风格字符串字面量转成 std::string，
    // 因为只有 std::string 才能用 + 做字符串拼接。
    std::string shaderPath = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/basicvertex.glsl";
    file.open(shaderPath);   // 尝试打开该文件

    // is_open() 返回流是否成功绑定了文件；取反表示“打开失败”。
    // 失败时打印路径方便排查，并返回非 0 值表示异常结束。
    if (!file.is_open()) {
        std::cout << "Failed to open shader: " << shaderPath << std::endl;
        return -1;
    }

    // std::getline(file, line) 每次从文件读一行到 line（不含换行符）。
    // 它在还能读到内容时返回真；读到文件末尾(EOF)或出错时返回假，循环结束。
    // std::endl 输出换行并刷新输出缓冲区。
    while (std::getline(file, line)) {
        std::cout << line << std::endl;
    }
    file.close();   // 手动关闭文件（其实离开作用域时析构也会自动关闭）

    // 打印一条分隔线，方便和后续输出区分
    std::cout << "------------------------" << std::endl;

    


    // ---------------------------------------------------------------
    // 【第 2 步】初始化 GLFW
    //
    // GLFW 负责窗口创建、OpenGL 上下文管理、键盘/鼠标输入等。
    // glfwInit() 返回 0 表示初始化失败（比如缺少系统支持）。
    // ---------------------------------------------------------------
    GLFWwindow* window;
    if (!glfwInit()) {
        std::cout << "Failed to initialize GLFW" << std::endl;
        return -1;
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
    window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "OpenGL Window", NULL, NULL);
    if (!window) {
        std::cout << "Failed to create window" << std::endl;
        glfwTerminate();
        return -1;
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
        return -1;
    }

    // 所有 OpenGL 调用必须在 GLAD 加载之后！
    // 设置清屏颜色（RGBA，取值范围 0.0~1.0）。
    // 这里是一种青绿色: R=0.2 G=0.3 B=0.3 A=1.0
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f); // 设置清屏颜色


    // Shader
    std::string vsPath = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/basicvertex.glsl";
    std::string fsPath = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/basicfrag.glsl";
    Shader shader(vsPath, fsPath);


    // 网格
    //   坐标是 NDC（标准化设备坐标），范围 -1~1，屏幕中心是原点，所以这三个点正好围成居中的三角形。片段着色器
    // vec4(vertexColor,1.0) 会把三个角的颜色插值，你会看到一个红绿蓝渐变的三角形。
    float vertices[] = {
        // 位置 x     y     z        颜色 r   g   b
         0.0f,  0.5f, 0.0f,        1.0f, 0.0f, 0.0f,   // 上顶点  -> 红
        -0.5f, -0.5f, 0.0f,        0.0f, 1.0f, 0.0f,   // 左下    -> 绿
         0.5f, -0.5f, 0.0f,        0.0f, 0.0f, 1.0f,   // 右下    -> 蓝
    };
    // - VBO 存的是"数据本身"（那一堆 float）。
    // - VAO 存的是"数据怎么取"——属性位置、类型、步长、偏移。Core Profile 下没有 VAO 就没法画。
    // - glVertexAttribPointer 的第 5 个参数 stride = 6*sizeof(float)，表示"每跨 6 个 float 是一个新顶点"；最后一个参数是偏移：位置从 0 开始，颜色从第 3 个 float 开始。
    unsigned int VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);                      // 下面配置的属性都记进 VAO

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW); // 数据上传到显卡

    // location = 0：位置（3 个 float）
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // location = 1：颜色（3 个 float），从第 3 个 float 开始
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);  

    // ---------------------------------------------------------------
    // 【第 6 步】渲染主循环
    //
    // glfwWindowShouldClose 返回窗口是否被要求关闭（比如点了关闭按钮）。
    // 返回假就一直循环，直到用户关闭窗口。
    // 目前循环里只清屏和刷新，还没有任何实际的绘制调用
    // （没有 glDrawArrays 等），也没有使用上面读到的着色器。
    // ---------------------------------------------------------------
    // 渲染循环
    while (!glfwWindowShouldClose(window)) {
        // 清空颜色缓冲（用 glClearColor 设置的颜色填充窗口）
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shader.ID);


        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);   // 从第 0 个顶点开始，取 3 个，组成三角形

        // 处理所有窗口事件（键盘输入、鼠标移动等）
        glfwPollEvents();

        glfwSwapBuffers(window); // 交换前后缓冲区（把画好的内容显示到屏幕）
    }
    // ---------------------------------------------------------------
    // 【第 7 步】清理并退出
    // glfwTerminate 释放 GLFW 占用的所有资源（窗口、上下文等）。
    // 返回 0 表示程序正常结束。
    // ---------------------------------------------------------------
    // 清理
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);

    glfwTerminate();

    return 0;
}
