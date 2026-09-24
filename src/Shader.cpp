#include "Shader.h"

// 构造函数
Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath)
    : mVertexPath(vertexPath), mFragmentPath(fragmentPath)  // 使用成员初始化列表初始化常量成员变量
{
   buildFromFiles(mVertexPath,mFragmentPath);
}

Shader::~Shader()
{
    if(ID!=0) glDeleteProgram(ID);
}

// 构建并使用
void Shader::Use()
{
    glUseProgram(ID);
}

// Shader
bool Shader::buildFromFiles(const std::string& vertexPath, const std::string& fragmentPath)
{
    // 读取顶点着色器源码
    std::string vertexSource = readShaderFile(vertexPath);
    if (vertexSource.empty()) {
        std::cout << "Failed to read vertex shader file: " << vertexPath << std::endl;
        return false;
    }

    // 读取片段着色器源码
    std::string fragmentSource = readShaderFile(fragmentPath);
    if (fragmentSource.empty()) {
        std::cout << "Failed to read fragment shader file: " << fragmentPath << std::endl;
        return false;
    }

    // 编译顶点着色器
    unsigned int vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    // 编译片段着色器
    unsigned int fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);

    // 创建着色器程序并链接
    ID = glCreateProgram();
    glAttachShader(ID, vertexShader);
    glAttachShader(ID, fragmentShader);
    glLinkProgram(ID);

    // 检查链接是否成功
    int success;
    glGetProgramiv(ID, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(ID, 512, nullptr, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
        return false;
    }

    // 删除着色器对象，它们已经链接到程序中，不再需要单独存在
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return true;
}

// ============================ 读取着色器文件函数 ============================
// const 表示参数 path 是只读的，函数内部不能修改它。
// & 表示按引用传递，避免拷贝 std::string 对象（节省性能）
std::string Shader::readShaderFile(const std::string& path)
{
    std::ifstream f(path); // 打开文件，打开这个文件，接入“水管”
    if (!f.is_open()) { // 如果文件没打开成功
        std::cout << "Failed to open shader file: " << path << std::endl;
        return ""; // 返回空字符串表示失败
    }
    // “水”的中转桶
    std::stringstream buffer; // 字符串流对象，像一个“水桶”，可以把文件内容读进来
    buffer << f.rdbuf(); // 把文件流的内容读进字符串流,文件里所有的原始内容（整个文件，一字不落）
    f.close(); // 关闭文件
    return buffer.str(); // 返回字符串流里的内容（整个文件的文本）
}

// ============================ 编译着色器函数 ============================
// unsigned int 是无符号整数类型，GLenum 是 OpenGL 自己定义的一种整数代号。
// GLenum: OpenGL 自己定义的一种整数代号。说白了就是：0x8B31 代表顶点，0x8B30 代表片段（人看不懂，所以起了个别名 GLenum）。你传进来告诉它"我要编译哪种 shader"。
unsigned int Shader::compileShader(GLenum shaderType, const std::string& shaderSource)
{
    // 编译 Shader 的步骤：
    // 1. 创建一个 shader 对象，返回它的 ID（工牌号
    // 2. 给 shader 对象设置源码
    // 3. 编译 shader 对象
    unsigned int shader = glCreateShader(shaderType); // 给Shader创建一个工牌，创建一个 shader 对象，返回它的 ID
    const char* src = shaderSource.c_str(); // std::string 转成 C 风格字符串（const char*），因为 OpenGL 只认 C 风格字符串
    // shader:工牌号、1：我就传一段代码，可以传好几段、&src：源码的地址、nullptr：源码长度，nullptr表示源码是以'\0'结尾的字符串
    glShaderSource(shader,1,&src, nullptr); // 给 shader 对象设置源码，告诉它源码在哪里
    glCompileShader(shader); // 编译 shader 对象

    // 检查编译是否成功
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success); // GL_COMPILE_STATUS = "我想查编译状态",&success = 把结果存在这个地址
    if (!success) {
        char infoLog[512]; // 借个能装 512 个字符的连续空位（相当于一个固定长度的小本子），等会儿装报错信息用。
        glGetShaderInfoLog(shader, 512, nullptr, infoLog); // 把报错信息塞进 infoLog 这个小本子里，最多塞 512 个字"。
        std::cout << "ERROR::SHADER::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    // 返回 shader 对象的 ID，后续可以用它来链接程序或删除 shader
    return shader;
}

// 设置MVP矩阵Uniform
void Shader::SetMatrix(const glm::mat4& ModelMatrix,const glm::mat4& ViewMatrix,const glm::mat4& ProjectionMatrix)
{
    glUniformMatrix4fv(glGetUniformLocation(ID, "ModelMatrix"), 1, GL_FALSE, glm::value_ptr(ModelMatrix));
    glUniformMatrix4fv(glGetUniformLocation(ID, "ViewMatrix"), 1, GL_FALSE, glm::value_ptr(ViewMatrix));
    glUniformMatrix4fv(glGetUniformLocation(ID, "ProjectionMatrix"), 1, GL_FALSE, glm::value_ptr(ProjectionMatrix));
}

// 设置Light Uniform
void Shader::SetLight(const glm::vec3& lightPos, const glm::vec3& lightColor)
{
    glUniform3fv(glGetUniformLocation(ID, "mainLightPos"), 1, glm::value_ptr(lightPos));
    glUniform3fv(glGetUniformLocation(ID, "mainLightColor"), 1, glm::value_ptr(lightColor));
}

// 设置Camera Uniform
void Shader::SetCamera(const glm::vec3& cameraPos)
{
    glUniform3fv(glGetUniformLocation(ID, "CameraPos"), 1, glm::value_ptr(cameraPos));
}