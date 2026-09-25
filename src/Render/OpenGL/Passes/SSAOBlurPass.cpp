#include "SSAOBlurPass.h"

#include <glad/glad.h>
#include "OpenGLShader.h"

#include <iostream>
#include <string>

SSAOBlurPass::~SSAOBlurPass()
{
    DestroyTargets();
}

bool SSAOBlurPass::Setup()
{
    mQuad.Create();

    mShader = std::make_unique<OpenGLShader>();
    const std::string vs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/fullscreen_vert.glsl";
    const std::string fs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/ssao_blur_frag.glsl";
    if (!mShader->BuildFromFiles(vs, fs)) {
        std::cerr << "[SSAOBlurPass] 着色器构建失败" << std::endl;
        return false;
    }
    return true;
}

void SSAOBlurPass::OnResize(int w, int h)
{
    if (w <= 0 || h <= 0)            return;
    if (w == mWidth && h == mHeight) return;
    if (!CreateTargets(w, h)) {
        std::cerr << "[SSAOBlurPass] 纹理创建失败（" << w << "x" << h << "）" << std::endl;
    }
}

bool SSAOBlurPass::CreateTargets(int w, int h)
{
    DestroyTargets();

    glGenTextures(1, &mBlurTex);
    glBindTexture(GL_TEXTURE_2D, mBlurTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &mFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mBlurTex, 0);

    const bool complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!complete) return false;

    mWidth  = w;
    mHeight = h;
    return true;
}

void SSAOBlurPass::DestroyTargets()
{
    if (mFBO)     glDeleteFramebuffers(1, &mFBO);
    if (mBlurTex) glDeleteTextures(1, &mBlurTex);
    mFBO = mBlurTex = 0;
    mWidth = mHeight = 0;
}

void SSAOBlurPass::Execute(OpenGLRenderContext& ctx)
{
    if (!mShader || !mFBO) return;
    if (ctx.ssaoRawTex == 0 || ctx.gbufferDepthTex == 0) return;   // 上游没产出就跳过

    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glViewport(0, 0, mWidth, mHeight);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    const float noOcclusion[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glClearBufferfv(GL_COLOR, 0, noOcclusion);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx.ssaoRawTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ctx.gbufferDepthTex);

    mShader->Use();
    mShader->SetInt("ssaoInput", 0);
    mShader->SetInt("gDepth",    1);

    mQuad.Draw();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, ctx.fbWidth, ctx.fbHeight);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);

    // ★ 把模糊后的 AO 交给后面的 Pass（BasePass 会用）
    ctx.ssaoTex    = mBlurTex;
    ctx.ssaoWidth  = mWidth;
    ctx.ssaoHeight = mHeight;
}
