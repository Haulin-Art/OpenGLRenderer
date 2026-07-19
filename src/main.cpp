#include "config.h"

int main(){
    //std::cout << "Hello OpenGL!" << std::endl;
    
    GLFWwindow* window;
    if (!glfwInit()) {
        std::cout << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // 设置 OpenGL 版本 (建议加上，明确使用现代 OpenGL)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // 创建窗口
    window = glfwCreateWindow(800, 600, "OpenGL Window", NULL, NULL);
    if (!window) {
        std::cout << "Failed to create window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window); // 设置当前上下文为新创建的窗口

    // GLAD 必须在 OpenGL 上下文创建之后初始化
    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        glfwTerminate();
        return -1;
    }

    // 所有 OpenGL 调用必须在 GLAD 加载之后！
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f); // 设置清屏颜色

    // 渲染循环
    while (!glfwWindowShouldClose(window)) {
        // 清空颜色缓冲（用 glClearColor 设置的颜色填充窗口）
        glClear(GL_COLOR_BUFFER_BIT);

        // 处理所有窗口事件（键盘输入、鼠标移动等）
        glfwPollEvents();

        glfwSwapBuffers(window); // 交换前后缓冲区
    }
    // 清理
    glfwTerminate();

    return 0;
}