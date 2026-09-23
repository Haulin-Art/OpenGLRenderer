# 设置 CMake 最低版本要求
cmake_minimum_required(VERSION 3.16)

# 配置程序名称，以及当前版本号，语言为C++
project(OpenGLRenderer VERSION 1.0.0 LANGUAGES C CXX)

# 查看系统当中的 OpenGL 库
find_package(OpenGL REQUIRED)

# 在这里配置文件
add_executable(OpenGLRenderer
    src/config.h
    src/main.cpp
    src/Mesh.h
    src/Mesh.cpp
    src/Shader.h
    src/Shader.cpp
    src/ObjLoader.h
    src/ObjLoader.cpp
    )

# 将项目根目录的绝对路径传给 C++，用于加载 shader 等资源文件
target_compile_definitions(OpenGLRenderer PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# ============================================
# 第三方库 1: GLAD — OpenGL 函数加载器
#
# GLAD 是一个 C 语言写的 OpenGL 加载库。
# 它的作用是: 在运行时查找显卡驱动提供的 OpenGL 函数地址。
#
# 为什么需要 GLAD？
#   Windows 自带的 OpenGL 只有 1.1 版本。
#   要使用 OpenGL 4.6 的功能 (如 DSA, 计算着色器等)，
#   必须通过显卡驱动的扩展机制动态加载函数指针。
#   GLAD 自动完成了这一切。
#
# 这里把 glad.c 直接编译进项目（静态库），
# 不需要额外安装任何东西。
# ============================================
add_library(glad STATIC                    # 静态库 (编译进 exe，不需要 .dll)
    dependencies/glad/src/glad.c                        # GLAD 的 C 源文件
)
# 告诉编译器去哪里找 glad 的头文件
target_include_directories(glad PUBLIC
    dependencies/glad/include                           # glad/glad.h 和 KHR/khrplatform.h
)

# ============================================
# 第三方库 2: GLFW — 窗口和输入管理
#
# GLFW 负责:
#   - 创建窗口 (glfwCreateWindow)
#   - 创建 OpenGL 上下文 (glfwMakeContextCurrent)
#   - 处理键盘/鼠标/滚轮输入 (回调函数)
#   - 处理窗口事件 (大小改变、关闭等)
#
# 这里使用预编译好的 GLFW 库文件（不需要从源码编译）。
# 项目里已经带了 MinGW 和 MSVC 两个版本的预编译库。
# ============================================

# GLFW_DIR: GLFW 库所在的目录
# CMAKE_SOURCE_DIR = CMakeLists.txt 所在的目录 = 项目根目录
set(GLFW_DIR ${CMAKE_SOURCE_DIR}/dependencies/glfw)

# IMPORTED: 告诉 CMake 这是一个"外部预编译库"（不是我们自己编译的）
add_library(glfw3 STATIC IMPORTED)

# Windows 平台的配置
if(WIN32)
    # ---- MinGW (GCC for Windows) ----
    # MINGW 是 CMake 的预定义变量，使用 MinGW 编译器时自动为真
    if(MINGW)
        # MinGW 版本的 GLFW 静态库
        # libglfw3.a 是 GCC 格式的静态库（相当于 MSVC 的 .lib）
        set_target_properties(glfw3 PROPERTIES
            IMPORTED_LOCATION ${GLFW_DIR}/lib-mingw-w64/libglfw3.a
        )
        # GLFW 在 Windows 上需要的系统库
        # opengl32: OpenGL 核心函数 (Windows 自带)
        # gdi32:    GDI 图形设备接口 (GLFW 用它创建窗口)
        target_link_libraries(glfw3 INTERFACE
            opengl32
            gdi32
        )

    # ---- MSVC (Visual Studio) ----
    else()
        # 根据不同的 Visual Studio 版本选择对应的预编译 .lib
        # MSVC_VERSION 是 CMake 的预定义变量:
        #   1930+ = VS 2022, 1920+ = VS 2019, 1910+ = VS 2017
        if(MSVC_VERSION GREATER_EQUAL 1930)
            set(GLFW_LIB_DIR ${GLFW_DIR}/lib-vc2022)
        elseif(MSVC_VERSION GREATER_EQUAL 1920)
            set(GLFW_LIB_DIR ${GLFW_DIR}/lib-vc2019)
        elseif(MSVC_VERSION GREATER_EQUAL 1910)
            set(GLFW_LIB_DIR ${GLFW_DIR}/lib-vc2017)
        else()
            set(GLFW_LIB_DIR ${GLFW_DIR}/lib-vc2015)
        endif()
        set_target_properties(glfw3 PROPERTIES
            IMPORTED_LOCATION ${GLFW_LIB_DIR}/glfw3.lib
        )
        target_link_libraries(glfw3 INTERFACE
            opengl32
            gdi32
        )
    endif()

# ---- Linux / macOS ----
else()
    # 在 Linux/macOS 上，需要用户先安装 GLFW:
    #   Ubuntu: sudo apt install libglfw3-dev
    #   macOS:  brew install glfw
    find_package(glfw3 REQUIRED)
    add_library(glfw3 ALIAS glfw)  # 创建别名，统一后续代码
endif()

# 头文件路径（所有平台都相同）
target_include_directories(glfw3 INTERFACE
    ${GLFW_DIR}/include
)

# ============================================
# 第三方库 3: GLM — 数学库 (OpenGL Mathematics)
#
# GLM 是纯头文件库，不需要编译成 .a/.lib。
# 它提供 mat4 / vec3 等类型，以及 perspective / lookAt / radians
# 等函数，用来计算 MVP 矩阵。
#
# INTERFACE 库: 本身不产生编译产物，只把"使用方法"(这里是头文件路径)
#               传递给链接它的目标。
#
# 头文件实际位于 dependencies/glm/glm/glm.hpp，
# 所以 include 目录指向 dependencies/glm，
# 代码里写 #include <glm/glm.hpp> 即可。
# ============================================
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE
    ${CMAKE_SOURCE_DIR}/dependencies/glm
)

# ============================================
# 第三方库 4: tinyobjloader — OBJ 模型解析器
#
# 单头文件库 (header-only)，不需要编译成 .a/.lib。
# 头文件位于 dependencies/tinyobjloader/tiny_obj_loader.h，
# 代码里写 #include "tiny_obj_loader.h" 即可。
#
# 注意: 它需要在某一个 .cpp 里 #define TINYOBJLOADER_IMPLEMENTATION
#       再 include，才能把实现编译出来（本项目的 ObjLoader.cpp 负责这件事）。
# ============================================
add_library(tinyobjloader INTERFACE)
target_include_directories(tinyobjloader INTERFACE
    ${CMAKE_SOURCE_DIR}/dependencies/tinyobjloader
)

# ============================================
# 链接：将 GLAD、GLFW、GLM、tinyobjloader 链接到主程序
# ============================================
target_link_libraries(OpenGLRenderer PRIVATE glad glfw3 OpenGL::GL glm tinyobjloader)