#pragma once
#include "OpenGLRenderPass.h"

#include <memory>

class OpenGLShader;             // 前向声明：头文件不需要认识它（也就不用拖进 glad）

// ============================================================
// GBufferPass —— 把场景的「世界法线」和「世界深度」写进两张纹理
//
// 它是第一个「渲染到自己的 RT」的 Pass，所以也是第一个真正需要 OnResize 的 Pass
// （RT 是屏幕尺寸的，窗口一变大就得重建）。
//
// 附件布局（一个 FBO、三个颜色附件 + 一个深度附件）：
//     COLOR_ATTACHMENT0  mNormalTex  RGBA16F  世界法线(xyz)   —— 留给以后放粗糙度/切线
//     COLOR_ATTACHMENT1  mDepthTex   R32F     世界深度         —— 到相机的距离（世界单位）
//     COLOR_ATTACHMENT2  mAlbedoTex  RGB8     材质固有色       —— ★ 没有它 SSGI 只能反弹"灰白的光"
//     DEPTH_ATTACHMENT   mDepthRBO   Renderbuffer(DEPTH24)
//                                    只给光栅化做遮挡判断用，不需要被采样
//
// ★ 为什么深度要分两份：
//     硬件深度缓冲是非线性的 [0,1] 值，做 SSAO/AO 时几乎总是要线性化和反算；
//     而"点到相机的距离"本身就是世界单位，读的人（以后的 AO / SSRT）拿来直接用。
//     代价是多一次带宽，好处是省掉一整套 near/far 反算。
//
// ★ 为什么清屏不用 glClearColor：
//     glClearColor 是【全局】状态，这里一改，下一帧主帧缓冲的 Clear() 就会被污染。
//     所以用 glClearBufferfv / glClearBufferfv(GL_DEPTH) —— 它们直接指定"清哪个附件、清成什么值"，
//     完全不碰全局状态。
//
// 产出（写进 ctx）：ctx.gbufferNormalTex / ctx.gbufferDepthTex / ctx.gbufferAlbedoTex
//                   / ctx.gbufferWidth / ctx.gbufferHeight
// ============================================================
class GBufferPass final : public OpenGLRenderPass {
public:
    GBufferPass() = default;
    ~GBufferPass() override;                 // ★ 释放 FBO / 两张纹理 / 深度 renderbuffer

    // ---- OpenGLRenderPass 接口 ----
    bool Setup() override;                   // 只编译着色器；RT 由 OnResize 建（因为它是屏幕尺寸的）
    void OnResize(int w, int h) override;    // 尺寸变了就重建 RT
    void Execute(OpenGLRenderContext& ctx) override;
    RenderPassStage Stage() const override { return RenderPassStage::GBuffer; }

private:
    // 背景像素的"哨兵深度"：比任何真实距离都大，读的人可以据此判断"这里没有几何"
    static constexpr float kNoGeometryDepth = 100.0f;

    bool CreateTargets(int w, int h);        // 建 FBO + 两个颜色附件 + 深度 renderbuffer
    void DestroyTargets();                   // 删掉上面全部

    // ---- 本 Pass 拥有的资源 ----
    unsigned int mFBO      = 0;
    unsigned int mNormalTex = 0;             // COLOR_ATTACHMENT0：世界法线
    unsigned int mDepthTex  = 0;             // COLOR_ATTACHMENT1：世界深度
    unsigned int mAlbedoTex = 0;             // COLOR_ATTACHMENT2：albedo（材质固有色）
    unsigned int mDepthRBO  = 0;             // DEPTH_ATTACHMENT（只给深度测试用）
    int          mWidth  = 0;
    int          mHeight = 0;

    std::unique_ptr<OpenGLShader> mShader;   // 画 G-Buffer 的着色器
};
