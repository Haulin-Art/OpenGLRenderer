#pragma once
#include "OpenGLRenderPass.h"
#include "FullscreenQuad.h"

#include <memory>

class OpenGLShader;

// ============================================================
// ScreenShadowPass —— 把 PCSS 从"每个材质的着色器"搬到"屏幕空间的一趟"
//
// 原来是：basicfrag 里每个像素都跑一遍 blocker search + 可变半径 PCF。
// 问题是：a) 同一片像素被多个物体重复覆盖时会重复算；b) 材质着色器越来越重；
//         c) 没法整体降分辨率、也没法单独给阴影做去噪。
//
// 现在是：对屏幕上的每个【可见】像素算一次，写进一张纹理，材质那边只采样一次。
//
// 输入（ctx）：gbufferNormalTex / gbufferDepthTex / shadowMapTex
//   + lightSpaceMatrix / viewMatrix / projectionMatrix / cameraPos
// 输出（ctx）：screenShadowRawTex
//
// ★ 降分辨率：默认半分辨率（kDownscale = 2）。
//   阴影本来就是低频的（边缘被半影抹开了），半分辨率看不出来，但省 4 倍。
//   上采样靠 BasePass 那边对纹理用 GL_LINEAR 双线性插值自然完成。
// ============================================================
class ScreenShadowPass final : public OpenGLRenderPass {
public:
    ScreenShadowPass() = default;
    ~ScreenShadowPass() override;

    bool Setup() override;
    void OnResize(int w, int h) override;      // 按 kDownscale 算出自己的尺寸
    void Execute(OpenGLRenderContext& ctx) override;
    RenderPassStage Stage() const override { return RenderPassStage::ScreenShadow; }

    static constexpr int kDownscale = 2;       // 2 = 半分辨率

private:
    bool CreateTargets(int w, int h);
    void DestroyTargets();

    unsigned int mFBO   = 0;
    unsigned int mTex   = 0;                   // R8：阴影因子，1 = 被照亮
    int          mWidth = 0;
    int          mHeight = 0;

    FullscreenQuad mQuad;
    std::unique_ptr<OpenGLShader> mShader;
};
