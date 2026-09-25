#pragma once
#include "OpenGLRenderPass.h"
#include "FullscreenQuad.h"

#include <memory>

class OpenGLShader;

// ============================================================
// SSGIBlurPass —— 把 SSGIPass 的间接光去噪（空间双边模糊）
//
// 输入：ssgiRawTex（未去噪的间接光）、gbufferDepthTex / gbufferNormalTex（当权重）
// 输出：ssgiTex（去噪后），BasePass 用它
//
// ★ 为什么必须单独一趟：
//   模糊要读"邻居的间接光"，而那张纹理正是 SSGIPass 这一趟写的
//   → 同一张纹理既读又写 = 反馈循环（未定义行为）。所以拆成两趟、两张纹理。
//   （和 SSAO / SSAOBlur 是同一个结构。）
//
// ★ 分辨率沿用半分辨率（和它的输入一致）；
//   BasePass 那边用 GL_LINEAR 采样，顺便完成放大到全屏。
// ============================================================
class SSGIBlurPass final : public OpenGLRenderPass {
public:
    SSGIBlurPass() = default;
    ~SSGIBlurPass() override;

    bool Setup() override;
    void OnResize(int w, int h) override;
    void Execute(OpenGLRenderContext& ctx) override;
    RenderPassStage Stage() const override { return RenderPassStage::SSGIBlur; }

private:
    bool CreateTargets(int w, int h);
    void DestroyTargets();

    static constexpr float kResolutionScale = 0.5f;   // 必须和 SSGIPass 一致

    unsigned int mFBO = 0;
    unsigned int mTex = 0;
    int          mWidth  = 0;
    int          mHeight = 0;

    FullscreenQuad mQuad;
    std::unique_ptr<OpenGLShader> mShader;
};
