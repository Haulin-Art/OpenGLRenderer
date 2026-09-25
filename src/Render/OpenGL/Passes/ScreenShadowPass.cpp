#include "ScreenShadowPass.h"

#include <glad/glad.h>
#include "OpenGLShader.h"

#include <iostream>
#include <string>
#include <vector>

ScreenShadowPass::~ScreenShadowPass()
{
    DestroyTargets();
}

bool ScreenShadowPass::Setup()
{
    mQuad.Create();

    mShader = std::make_unique<OpenGLShader>();
    const std::string vs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/fullscreen_vert.glsl";
    const std::string fs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/screen_shadow_frag.glsl";
    if (!mShader->BuildFromFiles(vs, fs)) {
        std::cerr << "[ScreenShadowPass] 着色器构建失败" << std::endl;
        return false;
    }
    return true;
}

void ScreenShadowPass::OnResize(int w, int h)
{
    if (w <= 0 || h <= 0) return;

    // ★ 降分辨率：按 kDownscale 缩小，并保证至少 1 像素
    const int sw = (w / kDownscale > 0) ? (w / kDownscale) : 1;
    const int sh = (h / kDownscale > 0) ? (h / kDownscale) : 1;
    if (sw == mWidth && sh == mHeight) return;

    if (!CreateTargets(sw, sh)) {
        std::cerr << "[ScreenShadowPass] 渲染目标创建失败（" << sw << "x" << sh << "）" << std::endl;
    }
}

bool ScreenShadowPass::CreateTargets(int w, int h)
{
    DestroyTargets();

    glGenTextures(1, &mTex);
    glBindTexture(GL_TEXTURE_2D, mTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);   // 上采样要线性
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &mFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTex, 0);

    const bool complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!complete) return false;

    mWidth  = w;
    mHeight = h;
    return true;
}

void ScreenShadowPass::DestroyTargets()
{
    if (mFBO) glDeleteFramebuffers(1, &mFBO);
    if (mTex) glDeleteTextures(1, &mTex);
    mFBO = mTex = 0;
    mWidth = mHeight = 0;
}

void ScreenShadowPass::Execute(OpenGLRenderContext& ctx)
{
    if (!mShader || !mFBO) return;
    if (ctx.gbufferNormalTex == 0 || ctx.gbufferDepthTex == 0) return;
    if (ctx.shadowMapTex == 0) return;

    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glViewport(0, 0, mWidth, mHeight);

    // 全屏 Pass：深度/混合/剔除全关
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    const float lit[4] = {1.0f, 1.0f, 1.0f, 1.0f};   // 默认"被照亮"
    glClearBufferfv(GL_COLOR, 0, lit);

    // 0 号：G-Buffer 法线；1 号：G-Buffer 深度；2 号：灯光空间深度图
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx.gbufferNormalTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ctx.gbufferDepthTex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ctx.shadowMapTex);

    mShader->Use();
    mShader->SetInt("gNormal", 0);
    mShader->SetInt("gDepth",  1);
    mShader->SetInt("shadowMap", 2);
    mShader->SetMat4("lightSpaceMatrix", ctx.lightSpaceMatrix);
    mShader->SetMat4("ViewMatrix",       ctx.viewMatrix);
    mShader->SetMat4("InvProjection",    glm::inverse(ctx.projectionMatrix));
    mShader->SetCamera(ctx.cameraPos);

    mQuad.Draw();

    // 收尾：切回主帧缓冲、恢复视口、解绑
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, ctx.fbWidth, ctx.fbHeight);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);

    ctx.screenShadowRawTex = mTex;
    ctx.screenShadowWidth  = mWidth;
    ctx.screenShadowHeight = mHeight;

    // 调试自检：阴影纹理也是"看不见"的（要等 BasePass 采样才看得出），
    // 读回来打一行统计。看够了改成 false。
    static bool s_dumpedOnce = false;
    static constexpr bool kDumpScreenShadow = true;
    if (kDumpScreenShadow && !s_dumpedOnce) {
        s_dumpedOnce = true;

        std::vector<unsigned char> px(static_cast<size_t>(mWidth) * mHeight);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, mFBO);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0, 0, mWidth, mHeight, GL_RED, GL_UNSIGNED_BYTE, px.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        int   shadowed = 0;      // 明显在阴影里的像素
        float sum = 0.0f;
        unsigned char lo = 255;
        for (unsigned char v : px) {
            if (v < lo) lo = v;
            sum += v / 255.0f;
            if (v < 128) ++shadowed;
        }
        std::cout << "[ScreenShadowPass] " << mWidth << "x" << mHeight
                  << "（半分辨率）  最暗=" << (int)lo << "/255"
                  << "  平均=" << (sum / static_cast<float>(px.size()))
                  << "  阴影占画面=" << (shadowed / static_cast<float>(px.size()) * 100.0f) << "%"
                  << std::endl;
    }
}
