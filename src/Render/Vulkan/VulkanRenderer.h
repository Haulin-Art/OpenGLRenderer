#pragma once
#include "Renderer.h"

class VulkanRenderer : public IRenderer {
    public:
        VulkanRenderer(int width, int height);
        ~VulkanRenderer() override;

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

    private:
        const int WINDOW_WIDTH = 800;
        const int WINDOW_HEIGHT = 600;
};