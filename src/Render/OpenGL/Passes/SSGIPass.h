#pragma once
#include "OpenGLRenderPass.h"
#include "FullscreenQuad.h"

#include <memory>

class OpenGLShader;   // 前向声明：头文件不用认识它

// ============================================================
// SSGIPass —— 屏幕空间全局光照（单次弹射），读 G-Buffer + 屏幕空间阴影，
//             算出一张【半分辨率】的屏幕空间间接光纹理
//
// 输入（从 ctx 拿）：
//     gbufferNormalTex / gbufferDepthTex   几何
//     gbufferAlbedoTex                     命中点的固有色 → 颜色渗透（★ 所以必须在 GBuffer 之后）
//     screenShadowTex                      命中点的可见性（★ 所以必须排在 ScreenShadowBlur 之后）
//     view / projection / cameraPos / lightPos / lightColor
// 输出（写回 ctx）：ssgiRawTex / ssgiWidth / ssgiHeight
//   ★ 注意这是【带噪点】的原始结果，紧接着由 SSGIBlurPass 去噪成 ssgiTex，
//     BasePass 采样的是后来那张。两张的分辨率相同。
//
// ★ 为什么它排在 ScreenShadowBlur(360) 之后 —— Stage 370：
//   屏幕空间步进"命中"的判定是 sampleDist ≈ sceneDist，
//   这意味着命中点【就是 uv_s 那个像素上可见的那块几何】。
//   既然是同一个点，在那个 UV 上采样屏幕空间阴影就是它的可见性，不会错位。
//   所以 SSGI 必须在屏幕空间阴影算完之后才能跑。
//
// ★ 开关：SetEnabled()。关掉时不是"跳过整个 Pass"，而是把纹理清成 0。
//   这样 BasePass 可以无条件地 `+ indirect` —— 关闭时的画面和没有 SSGI 时【逐像素一致】，
//   天然就是一个干净的 A/B 基准。
// ============================================================
class SSGIPass final : public OpenGLRenderPass {
public:
    SSGIPass() = default;
    ~SSGIPass() override;                       // ★ 释放 FBO / 间接光纹理

    // ---- OpenGLRenderPass 接口 ----
    bool Setup() override;                      // 编译着色器 + 建全屏三角形
    void OnResize(int w, int h) override;       // 尺寸变了就重建（建的是半分辨率）
    void Execute(OpenGLRenderContext& ctx) override;
    RenderPassStage Stage() const override { return RenderPassStage::SSGI; }

    // ---- 运行时开关（按 G 切换；见 OpenGLRenderer::PollEvents）----
    void SetEnabled(bool on) { mEnabled = on; }
    bool Enabled() const { return mEnabled; }

private:
    bool CreateTargets(int w, int h);
    void DestroyTargets();

    static constexpr float kResolutionScale = 0.5f;   // 半分辨率：SSGI 是低频效应

    bool         mEnabled = true;                    // 默认开启
    unsigned int mFBO     = 0;                       // 只挂一个颜色附件（全屏 Pass 不需要深度附件）
    unsigned int mTex     = 0;                       // RGBA16F：间接光是 HDR 的，不能用 R8
    int          mWidth   = 0;                       // 实际纹理尺寸（= 窗口尺寸 × kResolutionScale）
    int          mHeight  = 0;

    FullscreenQuad mQuad;
    std::unique_ptr<OpenGLShader> mShader;
};
