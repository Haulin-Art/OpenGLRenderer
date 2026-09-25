#include "SSGIBlurPass.h"

#include <glad/glad.h>
#include "OpenGLShader.h"

#include <algorithm>
#include <iostream>
#include <string>

SSGIBlurPass::~SSGIBlurPass()
{
    DestroyTargets();
}

bool SSGIBlurPass::Setup()
{
    mQuad.Create();

    mShader = std::make_unique<OpenGLShader>();
    const std::string vs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/fullscreen_vert.glsl";
    const std::string fs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/ssgi_blur_frag.glsl";
    if (!mShader->BuildFromFiles(vs, fs)) {
        std::cerr << "[SSGIBlurPass] 着色器构建失败" << std::endl;
        return false;
    }
    return true;
}

void SSGIBlurPass::OnResize(int w, int h)
{
    if (w <= 0 || h <= 0) return;

    // 尺寸必须和 SSGIPass 算出来的完全一致（两边都是同一个 kResolutionScale）
    const int hw = std::max(1, static_cast<int>(w * kResolutionScale));
    const int hh = std::max(1, static_cast<int>(h * kResolutionScale));

    if (hw == mWidth && hh == mHeight) return;
    if (!CreateTargets(hw, hh)) {
        std::cerr << "[SSGIBlurPass] 纹理创建失败（" << hw << "x" << hh << "）" << std::endl;
    }
}

bool SSGIBlurPass::CreateTargets(int w, int h)
{
    DestroyTargets();

    glGenTextures(1, &mTex);
    glBindTexture(GL_TEXTURE_2D, mTex);
    // 和输入保持同样的浮点格式（间接光是 HDR 的）
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
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

void SSGIBlurPass::DestroyTargets()
{
    if (mFBO) glDeleteFramebuffers(1, &mFBO);
    if (mTex) glDeleteTextures(1, &mTex);
    mFBO = mTex = 0;
    mWidth = mHeight = 0;
}

void SSGIBlurPass::Execute(OpenGLRenderContext& ctx)
{
    if (!mShader || !mFBO) return;
    // 上游没产出就跳过（SSGI 被关掉时它会清成 0 并照常发布纹理，所以这里不会跳）
    if (ctx.ssgiRawTex == 0 || ctx.gbufferDepthTex == 0 || ctx.gbufferNormalTex == 0) return;

    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glViewport(0, 0, mWidth, mHeight);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glClearBufferfv(GL_COLOR, 0, zero);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx.ssgiRawTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ctx.gbufferDepthTex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ctx.gbufferNormalTex);

    mShader->Use();
    mShader->SetInt("ssgiInput", 0);
    mShader->SetInt("gDepth",    1);
    mShader->SetInt("gNormal",   2);

    mQuad.Draw();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, ctx.fbWidth, ctx.fbHeight);

    // ★ 三张都要解绑：下一帧 SSGIPass / GBufferPass 要往它们里面写，
    //   不解绑就形成"同一张纹理既读又写"的反馈循环
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);

    // ★ 交给 BasePass 的是【去噪后】的这一张
    ctx.ssgiTex    = mTex;
    ctx.ssgiWidth  = mWidth;
    ctx.ssgiHeight = mHeight;
}
