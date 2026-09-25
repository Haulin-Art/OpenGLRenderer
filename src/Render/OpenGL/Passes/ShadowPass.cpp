#include "ShadowPass.h"

// ★ 谁用谁 include：这里直接调 gl*、用到 GL_* 枚举，就自己带上 glad
//   （不要指望"基类头里恰好 include 了 glad"，那样基类头一瘦身这里就会报
//    'GLenum does not name a type' —— 这个坑我写第一版时就踩到了）
#include <glad/glad.h>

#include "OpenGLShader.h"   // 头文件里只前向声明了它，这里才需要完整定义

#include <iostream>
#include <string>

// ============================================================
// 构造 / 析构
// ============================================================

ShadowPass::~ShadowPass() {
    // ★ 必须在 GL 上下文还活着时删 —— 也就是 renderer 的析构里、glfwTerminate() 之前。
    DestroyShadowMap();
    // mDepthShader 是 unique_ptr，析构时会调 OpenGLShader::~OpenGLShader → glDeleteProgram
}

// ============================================================
// 灯光空间矩阵（每帧从 ctx.lightPos 重算）
//
// 平行光用正交投影；near 不能是 0（否则灯背后的东西会被"投影"进来）。
// kOrthoHalfSize 必须恰好包住要投影的场景：太小 → 影子被切；太大 → 精度浪费、边缘毛刺。
// 顺带好处：光源一旦能动，影子会立刻跟上（不再像以前那样在 Setup 里算死一次）。
// ============================================================
void ShadowPass::UpdateLightMatrices(const glm::vec3& lightPos) {
    mLightProjection = glm::ortho(-kOrthoHalfSize, kOrthoHalfSize,
                                  -kOrthoHalfSize, kOrthoHalfSize,
                                   1.0f, 30.0f);
    mLightView       = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

// ============================================================
// Setup —— 只调一次（此时 GL 上下文和 GLAD 一定已就绪）
// ============================================================
bool ShadowPass::Setup() {
    // ---- 深度专用着色器 ----
    mDepthShader = std::make_unique<OpenGLShader>();
    const std::string vs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/shadow_mapping_depth_vert.glsl";
    const std::string fs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/shadow_mapping_depth_frag.glsl";
    if (!mDepthShader->BuildFromFiles(vs, fs)) {
        std::cerr << "[ShadowPass] 深度着色器构建失败" << std::endl;
        return false;
    }

    // ---- shadow map（FBO + 深度纹理）----
    if (!BuildShadowMap()) {
        std::cerr << "[ShadowPass] shadow map 创建失败" << std::endl;
        return false;
    }

    return true;
}

// ============================================================
// 建 shadow map
//
// ★ 为什么深度附件必须是 Texture 而不是 Renderbuffer：
//   主 Pass 的片元着色器要把它当 sampler2D 采样，而 Renderbuffer 不能采样。
// ============================================================
bool ShadowPass::BuildShadowMap() {
    glGenTextures(1, &mDepthTex);
    glBindTexture(GL_TEXTURE_2D, mDepthTex);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_DEPTH_COMPONENT24,          // 只要深度，24 位
                 kMapSize, kMapSize,
                 0,
                 GL_DEPTH_COMPONENT,
                 GL_FLOAT,
                 nullptr);                      // 先不给数据，我们要往里画
    // 阴影图必须 NEAREST：对深度值做线性插值没有物理意义
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // 环绕用 BORDER + border=1.0（"最远深度"）→ 灯光视锥之外判定为"被照亮"；
    // 用 CLAMP_TO_EDGE 的话边缘会出现假阴影条纹
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    glGenFramebuffers(1, &mFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER,
                           GL_DEPTH_ATTACHMENT,   // 挂在深度附件位置
                           GL_TEXTURE_2D, mDepthTex, 0);
    // ★ 必须的两行：告诉 GL "这个 FBO 没有颜色附件"
    //   注意 glDrawBuffer 是【每个 FBO 各自】的状态，不会影响别的 FBO
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    const bool complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);   // ★ 建完就解绑，避免以后和"当附件写"形成反馈循环
    return complete;
}

void ShadowPass::DestroyShadowMap() {
    if (mFBO)      glDeleteFramebuffers(1, &mFBO);
    if (mDepthTex) glDeleteTextures(1, &mDepthTex);
    mFBO      = 0;
    mDepthTex = 0;
}

// ============================================================
// 每帧执行
// ============================================================
void ShadowPass::Execute(OpenGLRenderContext& ctx) {
    if (!ctx.commands)          return;   // 没有队列就什么都不用干
    if (!mDepthShader || !mFBO) return;   // Setup 失败时的保护

    // 用本帧的光源位置算灯光空间矩阵
    UpdateLightMatrices(ctx.lightPos);

    // ---------------------------------------------------------
    // ① 切到 shadow RT
    // ---------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glViewport(0, 0, kMapSize, kMapSize);

    // ---------------------------------------------------------
    // ★ 渲染状态：【直接发，不做缓存】
    //   ShadowPass 要的状态和 BasePass 完全不同（要写深度、不要混合/剔除）。
    //   如果两个 Pass 各存一份"状态缓存"，它们对 GL 全局状态的认知必然互相打架
    //   —— 这个坑你已经踩过两次（Clear() 的 depth mask、shadow pass 的 depth mask）。
    //   正解是把状态缓存抽成【共享的】OpenGLStateCache，放进 ctx 让所有 Pass 共用；
    //   在那之前，物体这么少，几次多余的 glEnable 完全无感。
    // ---------------------------------------------------------
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    // ---------------------------------------------------------
    // ② 清深度（没有颜色附件，不用清颜色）
    // ---------------------------------------------------------
    glClear(GL_DEPTH_BUFFER_BIT);

    // ---------------------------------------------------------
    // ③ 画所有「投影者」
    //    复用 IShader::SetMatrix：把"灯光的 V/P"当 View/Projection 传进去
    //    （深度着色器用的是同名 uniform，所以一行都不用改）
    // ---------------------------------------------------------
    mDepthShader->Use();
    for (const RenderCommand& cmd : *ctx.commands) {
        if (!cmd.mesh || !cmd.material) continue;

        // TODO(S4): 用"是否透明"来筛投影者是权宜之计。
        //   "是否透明"和"是否投影"是两件事，正确做法是给 Material 加 bool castShadow。
        if (cmd.material->renderState.blend != BlendMode::Opaque) continue;

        mDepthShader->SetMatrix(TransformToModelMatrix(cmd.transform),
                                mLightView, mLightProjection);
        cmd.mesh->Draw();
    }

    // ---------------------------------------------------------
    // ④ 切回主帧缓冲 + 恢复视口
    //    ★ glViewport 是全局状态：谁改了谁负责还原，否则主 Pass 会画到错误区域
    // ---------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, ctx.fbWidth, ctx.fbHeight);

    // ---------------------------------------------------------
    // 把产出写进 ctx，给后面的 Pass 用
    // ---------------------------------------------------------
    ctx.lightSpaceMatrix = mLightProjection * mLightView;
    ctx.shadowMapTex     = mDepthTex;
}
