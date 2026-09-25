#include "SSGIPass.h"

#include <glad/glad.h>
#include "OpenGLShader.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

SSGIPass::~SSGIPass()
{
    // ★ 必须在 GL 上下文还活着时删
    DestroyTargets();
}

// ============================================================
// Setup —— 编译着色器 + 建全屏三角形
// （间接光纹理是"屏幕尺寸的一半"，要等 OnResize 送来尺寸）
// ============================================================
bool SSGIPass::Setup()
{
    mQuad.Create();

    mShader = std::make_unique<OpenGLShader>();
    const std::string vs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/fullscreen_vert.glsl";
    const std::string fs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/ssgi_frag.glsl";
    if (!mShader->BuildFromFiles(vs, fs)) {
        std::cerr << "[SSGIPass] 着色器构建失败" << std::endl;
        return false;
    }
    return true;
}

void SSGIPass::OnResize(int w, int h)
{
    if (w <= 0 || h <= 0) return;

    // ★ 半分辨率：SSGI 是低频效应，没必要全分辨率算。
    //   省 4 倍像素的射线步进，而且 BasePass 那边用 GL_LINEAR 采样会顺便做一次上采样平滑。
    const int hw = std::max(1, static_cast<int>(w * kResolutionScale));
    const int hh = std::max(1, static_cast<int>(h * kResolutionScale));

    if (hw == mWidth && hh == mHeight) return;
    if (!CreateTargets(hw, hh)) {
        std::cerr << "[SSGIPass] 间接光纹理创建失败（" << hw << "x" << hh << "）" << std::endl;
    }
}

// ============================================================
// 建 / 删间接光渲染目标
//
// 只要一个颜色附件，不需要深度附件（全屏 Pass 不参与深度测试）。
// 格式必须是【浮点】的：
//   间接光可以是 >1 的（多个方向的光叠加），而且量级很小（0.0x 级），
//   用 R8 会被量化成 0 或者被 1.0 截断 —— 这两件事都会让 SSGI 看起来"没生效"。
// ============================================================
bool SSGIPass::CreateTargets(int w, int h)
{
    DestroyTargets();

    glGenTextures(1, &mTex);
    glBindTexture(GL_TEXTURE_2D, mTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    // ★ 线性过滤：半分辨率的纹理要放大到全屏，靠它做双线性上采样（顺便磨掉一部分噪点）
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &mFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTex, 0);
    // 只有一个颜色附件 → 默认 draw buffer 就是 COLOR_ATTACHMENT0，不用显式设 glDrawBuffers

    const bool complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!complete) return false;

    mWidth  = w;
    mHeight = h;
    return true;
}

void SSGIPass::DestroyTargets()
{
    if (mFBO) glDeleteFramebuffers(1, &mFBO);
    if (mTex) glDeleteTextures(1, &mTex);
    mFBO = mTex = 0;
    mWidth = mHeight = 0;
}

// ============================================================
// 每帧执行
// ============================================================
void SSGIPass::Execute(OpenGLRenderContext& ctx)
{
    if (!mShader || !mFBO) return;
    // 没有 G-Buffer 就算不了（SSGI 的几何完全来自它）
    if (ctx.gbufferNormalTex == 0 || ctx.gbufferDepthTex == 0) return;
    // ★ 没有 albedo 就没有颜色渗透（命中点的辐射会退化成"只有亮度"的灰白反弹）
    if (ctx.gbufferAlbedoTex == 0) return;
    // ★ 没有屏幕空间阴影就不算：命中点的可见性要靠它，缺了会把"阴影里的几何"也当成光源
    if (ctx.screenShadowTex == 0) return;

    // ---- ① 切到间接光的 RT ----
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glViewport(0, 0, mWidth, mHeight);

    // 全屏 Pass 必须显式关掉这些（不然上一批物体留下的深度测试可能把整片片元丢掉）
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    // 用 glClearBufferfv 而不是 glClearColor：不碰全局清屏颜色
    const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // ---------------------------------------------------------
    // ② 关闭状态：清成 0 就收工
    //    不是"跳过整个 Pass" —— 那样纹理里会留着上一帧的旧值，
    //    BasePass 还是会把它加进画面（关不干净）。
    //    清成 0 之后，BasePass 加的间接光正好是 0，等价于这个 Pass 不存在。
    // ---------------------------------------------------------
    if (!mEnabled) {
        glClearBufferfv(GL_COLOR, 0, zero);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, ctx.fbWidth, ctx.fbHeight);

        ctx.ssgiRawTex = mTex;
        ctx.ssgiWidth  = mWidth;
        ctx.ssgiHeight = mHeight;
        return;
    }

    glClearBufferfv(GL_COLOR, 0, zero);

    // ---- ③ 绑定输入 ----
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx.gbufferNormalTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ctx.gbufferDepthTex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ctx.screenShadowTex);
    // ★ 3 号单元：命中点的 albedo（颜色渗透的来源）
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, ctx.gbufferAlbedoTex);

    mShader->Use();

    mShader->SetInt("gNormal",      0);
    mShader->SetInt("gDepth",       1);
    mShader->SetInt("screenShadow", 2);
    mShader->SetInt("gAlbedo",      3);

    mShader->SetMat4("ViewMatrix",    ctx.viewMatrix);
    mShader->SetMat4("Projection",    ctx.projectionMatrix);
    mShader->SetMat4("InvProjection", glm::inverse(ctx.projectionMatrix));
    mShader->SetCamera(ctx.cameraPos);
    mShader->SetLight(ctx.lightPos, ctx.lightColor);

    // ---- ④ 画全屏三角形 ----
    mQuad.Draw();

    // ---- ⑤ 收尾 ----
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, ctx.fbWidth, ctx.fbHeight);

    // ★ 四张输入纹理都要解绑：下一帧 SSGIPass / SSAOPass / ScreenShadowPass / GBufferPass
    //   要往它们里面写，不解绑就形成"同一张纹理既读又写"的反馈循环
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);

    ctx.ssgiRawTex = mTex;
    ctx.ssgiWidth  = mWidth;
    ctx.ssgiHeight = mHeight;

    // ---------------------------------------------------------
    // 调试自检：间接光是"看不见"的（要等 BasePass 加进去才看得出来），
    // 所以读回来打一行统计，确认它真的不是全 0。
    //   全 0 = 射线一根都没命中，或者衰减/判定写错了。
    //   想安静就把它改成 false。
    // ---------------------------------------------------------
    static bool s_dumpedOnce = false;
    static constexpr bool kDumpSSGI = true;
    if (kDumpSSGI && !s_dumpedOnce) {
        s_dumpedOnce = true;

        std::vector<float> px(static_cast<size_t>(mWidth) * mHeight * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, mFBO);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0, 0, mWidth, mHeight, GL_RGBA, GL_FLOAT, px.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        int   lit = 0;           // 有间接光的像素
        float sum = 0.0f;
        float hi  = 0.0f;
        const size_t n = static_cast<size_t>(mWidth) * mHeight;
        for (size_t i = 0; i < n; ++i) {
            const float* p = &px[i * 4];
            const float lum = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
            if (lum > 0.001f) ++lit;
            if (lum > hi) hi = lum;
            sum += lum;
        }
        std::cout << "[SSGIPass] 间接光 " << mWidth << "x" << mHeight
                  << "（半分辨率）  有间接光的像素=" << (lit / static_cast<float>(n) * 100.0f) << "%"
                  << "  最亮=" << hi
                  << "  平均亮度=" << (sum / static_cast<float>(n))
                  << std::endl;
    }
}
