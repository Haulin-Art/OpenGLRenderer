// # 继承 Renderer，真正调用 glDraw
#pragma once
#include "Renderer.h"

#include "OpenGLShader.h"
#include "OpenGLMesh.h"

#include <iostream>
#include <glad/glad.h>   // 这个得在GLFW之前引入，因为GLFW会使用OpenGL函数指针，而这些指针是由glad加载的
#include <GLFW/glfw3.h>  

class OpenGLRenderer : public IRenderer {
    public:
        GLFWwindow* window = nullptr;

        OpenGLRenderer(const int width, const int height);
        ~OpenGLRenderer();

        // 数据相关
        glm::vec2 GetWindowSize() override;

        // IRenderer 接口函数
        bool Init() override;
        void* GetWindow() override;
        void SetClearColor(const glm::vec4& color = glm::vec4(0.25f, 0.25f, 0.25f, 1.0f)) override;
        void Clear() override;

        // 封装API
        bool WindowShouldClose() override;
        void WindowTerminate() override;
        void SwapBuffers() override;
        void PollEvents() override;
        void EnableRendererFeature(BuiltInRendererFeatures feature) override;
        void DisableRendererFeature(BuiltInRendererFeatures feature) override;

        // 相关资源创建
        IShader* CreateShader() override;
        IMesh* CreateMesh() override;

        // 执行渲染队列命令
        void ExecuteRenderCommands(const std::vector<RenderCommand>& RenderingCommandQueue,const CameraData& RenderingCameraData) override;

        // 初始化ShadowPass
        void InitShadowPass();


    private:

        int WINDOW_WIDTH = 800;
        int WINDOW_HEIGHT = 600;
        bool CreateWindow();

        // 将 IRenderer 当中的 BuiltInRendererFeatures 转换为 OpenGL 的 GLenum
        GLenum ConvertBuiltInRendererFeaturesToGLenum(BuiltInRendererFeatures feature);

        // 设置渲染状态
        RenderState mRenderState;
        void ApplyRenderState(const RenderState& renderState);
        GLenum RenderStateToOpenGL(DepthFunc f);
        GLenum RenderStateToOpenGL(CullMode m);
        GLenum RenderStateToOpenGL(BlendMode b);


        // 阴影贴图相关
        // 这里后续添加阴影贴图渲染逻辑
        const int shadowMapSize = 1024;
        const glm::vec3 lightPos = glm::vec3(1.0f, 2.0f, 0.4f);   // 你现在写死的那个方向
        glm::mat4 lightProjection = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 1.0f, 30.0f); // 正交投影矩阵
        glm::mat4 lightView = glm::lookAt(glm::vec3(2.0f, 4.0f, 0.8f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)); // 视图矩阵
        OpenGLShader* mShadowShader = nullptr; // 阴影贴图着色器
        GLuint shadowTex01 = 0; // PCF阴影贴图纹理ID， 一个可以画的地方
        GLuint shadowFBO = 0; // 阴影贴图FBO ID，OpenGL 里实现 RT 的对象（一个容器）
};
