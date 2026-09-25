#pragma once
#include "OpenGLRenderPass.h"
#include "FullscreenQuad.h"

#include <memory>

class OpenGLShader;   // 前向声明：头文件不用认识它（也就不拖 glad）

// ============================================================
// SSAOPass —— 读 G-Buffer，算出一张屏幕空间的 AO 纹理
//
// 输入（从 ctx 拿）：gbufferNormalTex（世界法线）、gbufferDepthTex（世界深度）
//   + view/projection/cameraPos
// 输出（写回 ctx）：ssaoRawTex
//
// ★ 它是第一个"全屏 Pass"：不用画场景里的任何物体，而是对屏幕上每个像素算一次。
//   所以它需要一个 FullscreenQuad（3 个顶点盖住整个屏幕），
//   而且运行前必须把深度测试/混合/剔除全关掉。
//
// ★ 输出是【未模糊】的 AO —— 逐像素随机采样必然有噪点，
//   紧接着要跑 SSAOBlurPass 去磨平（不能在同一趟里模糊：会读到自己正在写的纹理）。
// ============================================================
class SSAOPass final : public OpenGLRenderPass {
public:
    SSAOPass() = default;
    ~SSAOPass() override;                    // ★ 释放 FBO / AO 纹理

    bool Setup() override;                   // 编译着色器 + 建全屏三角形
    void OnResize(int w, int h) override;    // 尺寸变了就重建 AO 纹理
    void Execute(OpenGLRenderContext& ctx) override;
    RenderPassStage Stage() const override { return RenderPassStage::AO; }

private:
    bool CreateTargets(int w, int h);
    void DestroyTargets();

    unsigned int mFBO   = 0;                 // 只挂一个单通道颜色附件，没有深度
    unsigned int mAOTex = 0;                 // R8：1 字节/像素，AO 在 [0,1] 够用了
    int          mWidth = 0;
    int          mHeight = 0;

    FullscreenQuad mQuad;
    std::unique_ptr<OpenGLShader> mShader;
};
