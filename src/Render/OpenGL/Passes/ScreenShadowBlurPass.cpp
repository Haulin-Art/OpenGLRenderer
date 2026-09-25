#include "ScreenShadowBlurPass.h"

#include <glad/glad.h>
#include "OpenGLShader.h"

#include <iostream>
#include <string>

ScreenShadowBlurPass::~ScreenShadowBlurPass()
{
    DestroyTargets();
}

bool ScreenShadowBlurPass::Setup()
{
    mQuad.Create();

    mShader = std::make_unique<OpenGLShader>();
    const std::string vs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/fullscreen_vert.glsl";
    const std::string fs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/screen_shadow_blur_frag.glsl";
    if (!mShader->BuildFromFiles(vs, fs)) {
        std::cerr << "[ScreenShadowBlurPass] 着色器构建失败" << std::endl;
        return false;
    }
    return true;
}

void ScreenShadowBlurPass::OnResize(int w, int h)
{
    if (w <= 0 || h <= 0) return;

    const int sw = (w / kDownscale > 0) ? (w / kDownscale) : 1;
    const int sh = (h / kDownscale > 0) ? (h / kDownscale) : 1;
    if (sw == mWidth && sh == mHeight) return;

    if (!CreateTargets(sw, sh)) {
        std::cerr << "[ScreenShadowBlurPass] 渲染目标创建失败（" << sw << "x" << sh << "）" << std::endl;
    }
}

bool ScreenShadowBlurPass::CreateTargets(int w, int h)
{
    DestroyTargets();

    glGenTextures(1, &mTex);
    glBindTexture(GL_TEXTURE_2D, mTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
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

void ScreenShadowBlurPass::DestroyTargets()
{
    if (mFBO) glDeleteFramebuffers(1, &mFBO);
    if (mTex) glDeleteTextures(1, &mTex);
    mFBO = mTex = 0;
    mWidth = mHeight = 0;
}

void ScreenShadowBlurPass::Execute(OpenGLRenderContext& ctx)
{
    if (!mShader || !mFBO) return;
    if (ctx.screenShadowRawTex == 0 || ctx.gbufferDepthTex == 0) return;

    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glViewport(0, 0, mWidth, mHeight);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    const float lit[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glClearBufferfv(GL_COLOR, 0, lit);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx.screenShadowRawTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ctx.gbufferDepthTex);

    mShader->Use();
    mShader->SetInt("blurInput", 0);
    mShader->SetInt("gDepth",    1);

    mQuad.Draw();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, ctx.fbWidth, ctx.fbHeight);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);

    // ★ 模糊后的阴影交给 BasePass
    ctx.screenShadowTex = mTex;
}
