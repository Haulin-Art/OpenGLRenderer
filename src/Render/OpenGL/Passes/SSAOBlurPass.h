#pragma once
#include "OpenGLRenderPass.h"
#include "FullscreenQuad.h"

#include <memory>

class OpenGLShader;

// ============================================================
// SSAOBlurPass —— 把 SSAOPass 的结果磨平
//
// 输入：ssaoRawTex（未模糊的 AO）、gbufferDepthTex（当权重用）
// 输出：ssaoTex（模糊后的 AO），BasePass 用它
//
// ★ 为什么必须单独一趟：
//   模糊要读"邻居的 AO"，而那个 AO 正是本 Pass 要写的纹理
//   → 同一张纹理既读又写 = 反馈循环（未定义行为）。所以只能拆成两趟、两张纹理。
//
// ★ 为什么按深度加权（不是普通盒子模糊）：
//   否则物体边缘两侧的 AO 会被混在一起，物体周围出现一圈亮边（halo）。
// ============================================================
class SSAOBlurPass final : public OpenGLRenderPass {
public:
    SSAOBlurPass() = default;
    ~SSAOBlurPass() override;

    bool Setup() override;
    void OnResize(int w, int h) override;
    void Execute(OpenGLRenderContext& ctx) override;
    RenderPassStage Stage() const override { return RenderPassStage::AOBlur; }

private:
    bool CreateTargets(int w, int h);
    void DestroyTargets();

    unsigned int mFBO   = 0;
    unsigned int mBlurTex = 0;
    int          mWidth = 0;
    int          mHeight = 0;

    FullscreenQuad mQuad;
    std::unique_ptr<OpenGLShader> mShader;
};
