// # 继承 Renderer，真正调用 glDraw
#pragma once
#include "Renderer.h"

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
        void Render(Mesh* mesh, Shader* shader, glm::mat4& transform) override;
        void SetClearColor(const glm::vec4& color = glm::vec4(0.25f, 0.25f, 0.25f, 1.0f)) override;
        void Clear() override;

        // 封装API
        bool WindowShouldClose() override;
        void SwapBuffers() override;
        void PollEvents() override;
        void EnableRendererFeature(BuiltInRendererFeatures feature) override;
        void DisableRendererFeature(BuiltInRendererFeatures feature) override;

        // 执行渲染队列命令
        void ExecuteRenderCommands(const std::vector<RenderCommand>& RenderingCommandQueue,const CameraData& RenderingCameraData) override;
    private:
        const int WINDOW_WIDTH = 800;
        const int WINDOW_HEIGHT = 600;
        bool CreateWindow();

        // 将 IRenderer 当中的 BuiltInRendererFeatures 转换为 OpenGL 的 GLenum
        GLenum ConvertBuiltInRendererFeaturesToGLenum(BuiltInRendererFeatures feature);

        // 设置渲染状态
        RenderState mRenderState;
        void ApplyRenderState(const RenderState& renderState);
        GLenum RenderStateToOpenGL(DepthFunc f);
        GLenum RenderStateToOpenGL(CullMode m);
        GLenum RenderStateToOpenGL(BlendMode b);
};
