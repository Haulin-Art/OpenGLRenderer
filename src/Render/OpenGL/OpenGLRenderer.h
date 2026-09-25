// # 继承 Renderer，真正调用 glDraw
#pragma once
#include "Renderer.h"

#include "OpenGLShader.h"
#include "OpenGLMesh.h"

// Pass 基类 + 帧上下文（头文件很瘦：只有 RenderCommand / vector / glm，不含 glad）
#include "Passes/OpenGLRenderPass.h"

#include <iostream>
#include <memory>
#include <vector>
#include <glad/glad.h>   // 这个得在GLFW之前引入，因为GLFW会使用OpenGL函数指针，而这些指针是由glad加载的
#include <GLFW/glfw3.h>  

class SSGIPass;   // 只用来持有一个【非拥有】的指针（按 G 切换开关用）；定义在 .cpp 里 include

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




    private:

        int WINDOW_WIDTH = 800;
        int WINDOW_HEIGHT = 600;
        bool CreateWindow();

        // 将 IRenderer 当中的 BuiltInRendererFeatures 转换为 OpenGL 的 GLenum
        GLenum ConvertBuiltInRendererFeaturesToGLenum(BuiltInRendererFeatures feature);

        // 设置渲染状态
        // ★ 注意：按"材质"应用渲染状态（ApplyRenderState）已经搬到 BasePass 里了。
        //   mRenderState 现在只被 Clear() 用来同步 depthWrite（因为 glDepthMask 影响 glClear）。
        //   正确形态是把状态缓存抽成一个【共享的】OpenGLStateCache，放进 ctx 让所有 Pass 共用
        //   —— 那样缓存只有一份，才可能和 GL 真实状态保持一致。
        RenderState mRenderState;

        // ============================================================
        // 渲染管线：一串按 Stage() 排好序的 Pass
        //
        // 想加新 Pass：建一个类（继承 OpenGLRenderPass）→ 在 Init() 里 emplace_back
        // → 设好 Stage()。不需要改别的 Pass，也不需要改 ExecuteRenderCommands。
        //
        // 规划中的阶段（数值小的先跑；✅ 已实现 / ⬜ 待做）：
        //     ✅ Shadow(100) → ✅ G-Buffer(200) → ✅ AO(300) → ✅ AOBlur(310)
        //     → ✅ ScreenShadow(350) → ✅ ScreenShadowBlur(360) → ✅ SSGI(370)
        //     → ⬜ Background(400) → ✅ Opaques(500) → ⬜ Transparents(600)
        //     → ⬜ PostProcess(800) → ⬜ SSRT(850) → ⬜ ColorGradient(900)
        //   （Background / SSRT / ColorGradient 还没加进 RenderPassStage 枚举）
        // ============================================================
        std::vector<std::unique_ptr<OpenGLRenderPass>> mPasses;

        // SSGI 的【非拥有】指针（对象归 mPasses 里的 unique_ptr 管）。
        // 只用来做运行时开关：按 G 切换（见 PollEvents）。
        // ★ 在 Init() 里 emplace_back 【之前】取 .get()，之后 vector 怎么排序都不影响它。
        SSGIPass* mSSGI = nullptr;
        bool      mSSGIKeyHeld = false;   // 防止"按住 G"在一帧里连翻好几次

        // 用来判断"窗口尺寸变了"，变了才通知各 Pass（避免每帧都调 OnResize）
        int mLastFBWidth  = 0;
        int mLastFBHeight = 0;

        // G-Buffer
        // ★ 原来在这里的 GBufferFBO / ScaneDepTex / GBufferA / mGBufferShader / InitGBufferPass()
        //   已经全部搬进 Passes/GBufferPass.{h,cpp} —— 资源由那个 Pass 自己持有（RAII）。
        //   这里不再保留任何"渲染器级别的中间 RT"，这正是 Pass 化的意义。


        // 光源（渲染器暂时替场景保管；将来由 Scene/LightData 提供）
        // 每帧会被填进 OpenGLRenderContext，供需要它的 Pass 使用
        const glm::vec3 lightPos   = glm::vec3(1.0f, 2.0f, 0.4f);
        glm::vec3       lightColor = glm::vec3(1.0f, 1.0f, 1.0f);
};
