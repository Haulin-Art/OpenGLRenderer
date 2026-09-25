#pragma once
#include "IShader.h" // Shader抽象接口，目的是解耦底层API（OpenGL、Vulkan、DirectX等）和上层渲染逻辑。上层渲染逻辑只需要关心Shader的接口，而不需要关心底层API的具体实现。

#include <iostream>
#include <glad/glad.h>   // 这个得在GLFW之前引入，因为GLFW会使用OpenGL函数指针，而这些指针是由glad加载的
#include <GLFW/glfw3.h>  
// iostream , 控制台输入输出
// glfw3 ， 跨平台窗口库
// glad， OpenGL函数加载库
#include <fstream> // 文件流， 用于读取文件
#include <sstream> // 字符串流， 用于将文件内容读入字符串
#include <string>  // 字符串类

class OpenGLShader : public IShader {
    
    public:
        OpenGLShader();
        ~OpenGLShader() override;

        // IShader接口实现
        unsigned int GetID() const override { return m_ID; }; // 获取着色器程序ID
        void Use() override; // 使用着色器程序
        void SetMatrix(const glm::mat4& ModeMatrix,const glm::mat4& ViewMatrix,const glm::mat4& ProjectionMatrix) override; // 设置MVP矩阵Uniform
        void SetLight(const glm::vec3& lightPos, const glm::vec3& lightColor) override; // 设置灯光Uniform
        void SetCamera(const glm::vec3& cameraPos) override; // 设置摄像机相关Uniform
        bool BuildFromFiles(const std::string& vertexPath, const std::string& fragmentPath);

        // Shadow Pass 嵌入式
        void SetMat4(const std::string& name, const glm::mat4& value) override;
        void SetInt (const std::string& name, int value) override;
        
    private:
        unsigned int m_ID = 0; // 着色器程序ID
        const std::string mVertexPath;   // 顶点着色器文件路径
        const std::string mFragmentPath; // 片段着色器文件路径

        std::string readShaderFile(const std::string& path);
        unsigned int compileShader(GLenum shaderType, const std::string& shaderSource); 
};