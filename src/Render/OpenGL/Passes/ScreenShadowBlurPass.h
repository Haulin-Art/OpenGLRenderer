#pragma once
#include "OpenGLRenderPass.h"
#include "FullscreenQuad.h"

#include <memory>

class OpenGLShader;

// ============================================================
// ScreenShadowBlurPass —— 把 ScreenShadowPass 的结果去噪
//
// PCSS 的第一步（blocker search）用少量采样估"遮挡物平均深度"，
// 结果在相邻像素之间会跳变 → 阴影边缘斑驳。这里用"按深度加权的双边模糊"磨平。
//
// 输入（ctx）：screenShadowRawTex + gbufferDepthTex
// 输出（ctx）：screenShadowTex（BasePass 采样这一张）
//
// ★ 为什么必须单独一趟：模糊要读邻居的阴影值，而那正是本 Pass 要写的纹理
//   → 同纹理既读又写 = 反馈循环。所以只能两张纹理、两趟。
// ============================================================
class ScreenShadowBlurPass final : public OpenGLRenderPass {
public:
    ScreenShadowBlurPass() = default;
    ~ScreenShadowBlurPass() override;

    bool Setup() override;
    void OnResize(int w, int h) override;
    void Execute(OpenGLRenderContext& ctx) override;
    RenderPassStage Stage() const override { return RenderPassStage::ScreenShadowBlur; }

    // ★ 必须和 ScreenShadowPass::kDownscale 保持一致（两张纹理是同一分辨率）
    static constexpr int kDownscale = 2;

private:
    bool CreateTargets(int w, int h);
    void DestroyTargets();

    unsigned int mFBO   = 0;
    unsigned int mTex   = 0;
    int          mWidth = 0;
    int          mHeight = 0;

    FullscreenQuad mQuad;
    std::unique_ptr<OpenGLShader> mShader;
};
