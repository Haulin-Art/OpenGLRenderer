#pragma once
#include <iostream>
#include <glad/glad.h>   // 这个得在GLFW之前引入，因为GLFW会使用OpenGL函数指针，而这些指针是由glad加载的
#include <GLFW/glfw3.h>  
// iostream , 控制台输入输出
// glfw3 ， 跨平台窗口库
// glad， OpenGL函数加载库
#include <fstream> // 文件流， 用于读取文件
#include <sstream> // 字符串流， 用于将文件内容读入字符串
#include <string>  // 字符串类

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>   // glm::value_ptr，传给 glUniformMatrix4fv 用

class Shader
{
    public:
        unsigned int ID = 0; // 着色器程序ID
        // 构造函数，传入顶点着色器和片段着色器的文件路径
        Shader(const std::string& vertexPath, const std::string& fragmentPath);
        // 析构函数
        ~Shader();

        void Use(); // 使用着色器程序

        // 设置MVP矩阵Uniform
        void SetMatrix(const glm::mat4& ModeMatrix,const glm::mat4& ViewMatrix,const glm::mat4& ProjectionMatrix);

        // 设置灯光Uniform
        void SetLight(const glm::vec3& lightPos, const glm::vec3& lightColor);

        // 设置摄像机相关Uniform
        void SetCamera(const glm::vec3& cameraPos);

    private:
        const std::string mVertexPath;   // 顶点着色器文件路径
        const std::string mFragmentPath; // 片段着色器文件路径
        // ======================== 从文件读取着色器源码并编译链接成程序 ==============
        bool buildFromFiles(const std::string& vertexPath, const std::string& fragmentPath);
        // ============================ 读取着色器文件函数 ============================
        std::string readShaderFile(const std::string& path);
        // ============================ 编译着色器函数 ============================
        unsigned int compileShader(GLenum shaderType, const std::string& shaderSource);
};

