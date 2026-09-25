// 渲染后端接口
#pragma once

#include "RenderCommand.h"

#include "IMesh.h"
#include "IShader.h"
#include "Camera.h"

// 底层API内置渲染特性
enum class BuiltInRendererFeatures{
    DepthTest,    // 深度测试
    Blend,        // 混合
    CullFace,     // 面剔除
    StencilTest,  // 模板测试
    Multisample,  // 多重采样
    ScissorTest   // 裁剪测试
};


class IRenderer {
    public:
        // 数据相关
        virtual glm::vec2 GetWindowSize() = 0; // 获取窗口大小

        // 窗口与上下文相关
        virtual ~IRenderer() = default;
        virtual bool Init() = 0;  // 初始化，创建窗口
        virtual void* GetWindow() = 0; // 获取窗口
        virtual void SetClearColor(const glm::vec4& color = glm::vec4(0.2f, 0.3f, 0.3f, 1.0f)) = 0; // 设置清空颜色
        virtual void Clear() = 0;   // 清空颜色缓冲区

        // 封装API
        virtual bool WindowShouldClose() = 0; // 窗口是否应该关闭
        virtual void WindowTerminate() = 0; // 终止窗口
        virtual void SwapBuffers() = 0; // 交换缓冲区
        virtual void PollEvents() = 0; // 轮询事件
        virtual void EnableRendererFeature(BuiltInRendererFeatures feature) = 0; // 启用渲染特性
        virtual void DisableRendererFeature(BuiltInRendererFeatures feature) = 0; // 禁用渲染特性
        //virtual void CreateTexture(unsigned int textureId, int width, int height) = 0;

        // 相关资源创建
        virtual IShader* CreateShader() = 0; // 创建着色器
        virtual IMesh* CreateMesh() = 0; // 创建网格

        // 渲染队列
        // 这个渲染队列是不是得用指针？？？？
        virtual void ExecuteRenderCommands(const std::vector<RenderCommand>& RenderCommandQueue,const CameraData& RenderingCameraData) = 0; // 执行所有渲染命令
};
// 创建渲染器实例
// 全局作用域
IRenderer* CreateRenderer(int width, int height);