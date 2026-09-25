#include "SSAOPass.h"

#include <glad/glad.h>
#include "OpenGLShader.h"

#include <iostream>
#include <string>
#include <vector>

SSAOPass::~SSAOPass()
{
    // ★ 必须在 GL 上下文还活着时删
    DestroyTargets();
}

// ============================================================
// Setup —— 编译着色器 + 建全屏三角形的 VAO
// （AO 纹理是屏幕尺寸，要等 OnResize 送来尺寸）
// ============================================================
bool SSAOPass::Setup()
{
    mQuad.Create();

    mShader = std::make_unique<OpenGLShader>();
    const std::string vs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/fullscreen_vert.glsl";
    const std::string fs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/ssao_frag.glsl";
    if (!mShader->BuildFromFiles(vs, fs)) {
        std::cerr << "[SSAOPass] 着色器构建失败" << std::endl;
        return false;
    }
    return true;
}

void SSAOPass::OnResize(int w, int h)
{
    if (w <= 0 || h <= 0)               return;
    if (w == mWidth && h == mHeight)    return;
    if (!CreateTargets(w, h)) {
        std::cerr << "[SSAOPass] AO 纹理创建失败（" << w << "x" << h << "）" << std::endl;
    }
}

// ============================================================
// 建 / 删 AO 渲染目标
//
// 只要一个颜色附件，不需要深度附件（全屏 Pass 不参与深度测试）。
// R8 就够：AO 是 [0,1] 的灰度，256 级足够，而且带宽只有 R32F 的 1/4。
// ============================================================
bool SSAOPass::CreateTargets(int w, int h)
{
    DestroyTargets();

    glGenTextures(1, &mAOTex);
    glBindTexture(GL_TEXTURE_2D, mAOTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);   // 模糊/上采样要线性
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &mFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mAOTex, 0);
    // 只有一个颜色附件，默认的 draw buffer 就是 COLOR_ATTACHMENT0，不用显式设

    const bool complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!complete) return false;

    mWidth  = w;
    mHeight = h;
    return true;
}

void SSAOPass::DestroyTargets()
{
    if (mFBO)   glDeleteFramebuffers(1, &mFBO);
    if (mAOTex) glDeleteTextures(1, &mAOTex);
    mFBO = mAOTex = 0;
    mWidth = mHeight = 0;
}

// ============================================================
// 每帧执行
// ============================================================
void SSAOPass::Execute(OpenGLRenderContext& ctx)
{
    if (!mShader || !mFBO) return;
    // 没有 G-Buffer 就没法算 AO（比如 G-Buffer Pass 还没跑或失败了）
    if (ctx.gbufferNormalTex == 0 || ctx.gbufferDepthTex == 0) return;

    // ---- ① 切到 AO 的 RT ----
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glViewport(0, 0, mWidth, mHeight);

    // ★ 全屏 Pass 必须显式关掉这些：
    //   ① 它们本来就没意义（没有深度附件、不混合、不需要剔面）
    //   ② 更重要的是：如果留着上一批物体设的深度测试，全屏三角形的片元可能被整片丢掉
    //      → 症状是"这个 Pass 什么都没画出来"，非常难查
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    // 用 glClearBufferfv 而不是 glClearColor：不碰全局的清屏颜色（否则会污染主帧缓冲的 Clear）
    const float noOcclusion[4] = {1.0f, 1.0f, 1.0f, 1.0f};   // AO = 1 表示"完全不遮挡"
    glClearBufferfv(GL_COLOR, 0, noOcclusion);

    // ---- ② 绑定 G-Buffer 的两张纹理 ----
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx.gbufferNormalTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ctx.gbufferDepthTex);

    mShader->Use();
    mShader->SetInt("gNormal", 0);
    mShader->SetInt("gDepth",  1);
    mShader->SetMat4("ViewMatrix",    ctx.viewMatrix);
    mShader->SetMat4("Projection",    ctx.projectionMatrix);
    mShader->SetMat4("InvProjection", glm::inverse(ctx.projectionMatrix));  // 用来算每像素的视线方向
    mShader->SetCamera(ctx.cameraPos);

    // ---- ③ 画全屏三角形 ----
    mQuad.Draw();

    // ---- ④ 收尾：切回主帧缓冲、恢复视口、解绑纹理 ----
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, ctx.fbWidth, ctx.fbHeight);
    // ★ 一定要解绑：不解绑的话下一帧把它当渲染目标写时，就形成"同一张纹理既读又写"的反馈循环
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);   // 0 号单元保持绑定为空是惯例

    ctx.ssaoRawTex = mAOTex;

    // ---------------------------------------------------------
    // 调试自检：AO 是"看不见"的（要等 BasePass 乘进光照才看得出来），
    // 所以这里读回来打一行统计，确认它真的算出了东西。
    //   背景/平面 → AO ≈ 1；猴头的凹陷处 → AO 明显 < 1（被遮挡）
    // 看够了就把它改成 false（或者直接删掉这段）。
    // ---------------------------------------------------------
    static bool s_dumpedOnce = false;
    static constexpr bool kDumpSSAO = true;
    if (kDumpSSAO && !s_dumpedOnce) {
        s_dumpedOnce = true;

        std::vector<unsigned char> px(static_cast<size_t>(mWidth) * mHeight);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, mFBO);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0, 0, mWidth, mHeight, GL_RED, GL_UNSIGNED_BYTE, px.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        int   occ = 0;          // 被明显遮挡的像素数
        int   full = 0;         // 完全没被遮挡
        float sum = 0.0f;
        unsigned char lo = 255;
        for (unsigned char v : px) {
            if (v < lo) lo = v;
            sum += v / 255.0f;
            if (v < 230) ++occ;
            if (v >= 254) ++full;
        }
        const float total = static_cast<float>(px.size());
        std::cout << "[SSAOPass] AO " << mWidth << "x" << mHeight
                  << "  最低=" << (int)lo << "/255"
                  << "  平均=" << (sum / total)
                  << "  被遮挡(<0.9)的像素=" << (occ / total * 100.0f) << "%"
                  << "  完全无遮挡=" << (full / total * 100.0f) << "%"
                  << std::endl;
    }
}
