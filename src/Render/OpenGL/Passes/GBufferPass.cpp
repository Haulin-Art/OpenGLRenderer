#include "GBufferPass.h"

// ★ 谁用谁 include：这里直接调 gl*，就自己带上 glad
#include <glad/glad.h>

#include "OpenGLShader.h"   // 头文件里只前向声明了它

#include <iostream>
#include <string>
#include <vector>

// ============================================================
// 构造 / 析构
// ============================================================

GBufferPass::~GBufferPass() {
    // ★ 必须在 GL 上下文还活着时删（renderer 的 mPasses.clear() 在 glfwTerminate 之前）
    DestroyTargets();
}

// ============================================================
// Setup —— 只编译着色器
//
// 为什么 RT 不在这里建：它是【屏幕尺寸】的，而 Setup() 拿不到尺寸。
// 尺寸由 OnResize(w, h) 送来 —— renderer 在 Init() 里 Setup 之后会立刻调一次
// OnResize（见 OpenGLRenderer::Init），所以第一帧之前 RT 一定已经建好了。
// ============================================================
bool GBufferPass::Setup() {
    mShader = std::make_unique<OpenGLShader>();
    const std::string vs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/gbuffer_vert.glsl";
    const std::string fs = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/gbuffer_frag.glsl";
    if (!mShader->BuildFromFiles(vs, fs)) {
        std::cerr << "[GBufferPass] 着色器构建失败" << std::endl;
        return false;
    }
    return true;
}

// ============================================================
// 窗口尺寸变化 → 重建 RT
// ============================================================
void GBufferPass::OnResize(int w, int h) {
    if (w <= 0 || h <= 0)   return;
    if (w == mWidth && h == mHeight) return;   // 尺寸没变就不折腾

    if (!CreateTargets(w, h)) {
        std::cerr << "[GBufferPass] G-Buffer 创建失败（" << w << "x" << h << "）" << std::endl;
    }
}

// ============================================================
// 建 / 删 G-Buffer
// ============================================================
bool GBufferPass::CreateTargets(int w, int h) {
    DestroyTargets();

    // ---- 颜色附件 0：世界法线 ----
    // RGBA16F：半精度浮点足够存归一化法线，带宽只有 RGBA32F 的一半
    glGenTextures(1, &mNormalTex);
    glBindTexture(GL_TEXTURE_2D, mNormalTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);  // 法线不做插值
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // ---- 颜色附件 1：世界深度（单通道浮点）----
    glGenTextures(1, &mDepthTex);
    glBindTexture(GL_TEXTURE_2D, mDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // ---- 颜色附件 2：albedo（材质固有色）----
    // RGB8 就够：albedo 是 [0,1] 的"颜色"，不是 HDR 光，不需要浮点，也不需要 alpha。
    //   带宽只有 RGBA16F 的 1/4（法线那张必须浮点，但颜色不用 —— 存的是 8 位/通道的固有色）。
    // ★ 过滤方式必须和另外两张一致用 NEAREST：
    //   G-Buffer 是"逐像素的属性表"，读它的人（SSGI 采样命中点的 albedo）
    //   要的就是"那个像素上的材质是什么颜色"。线性插值会把相邻两个不同材质的颜色混起来，
    //   于是"红色猴头的反射"会带着一圈灰色的边。
    glGenTextures(1, &mAlbedoTex);
    glBindTexture(GL_TEXTURE_2D, mAlbedoTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // ---- 深度附件：用 Renderbuffer 就够了 ----
    //    它只参与光栅化的深度测试，不需要被着色器采样（要采样的是上面那张线性深度）
    glGenRenderbuffers(1, &mDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, mDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

    // ---- 组装 FBO ----
    glGenFramebuffers(1, &mFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mNormalTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, mDepthTex,  0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, mAlbedoTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mDepthRBO);

    // ★★ MRT 的关键一步：必须显式告诉 GL「这次要往哪几个附件写」。
    //    默认只有 COLOR_ATTACHMENT0 是打开的，不写这句的话 gDepth 永远写不进去。
    const GLenum drawBuffers[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    glDrawBuffers(3, drawBuffers);

    const bool complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!complete) return false;

    mWidth  = w;
    mHeight = h;
    return true;
}

void GBufferPass::DestroyTargets() {
    if (mFBO)      glDeleteFramebuffers(1, &mFBO);
    if (mNormalTex) glDeleteTextures(1, &mNormalTex);
    if (mDepthTex)  glDeleteTextures(1, &mDepthTex);
    if (mAlbedoTex) glDeleteTextures(1, &mAlbedoTex);
    if (mDepthRBO)  glDeleteRenderbuffers(1, &mDepthRBO);
    mFBO = mNormalTex = mDepthTex = mAlbedoTex = mDepthRBO = 0;
    mWidth = mHeight = 0;
}

// ============================================================
// 每帧执行
// ============================================================
void GBufferPass::Execute(OpenGLRenderContext& ctx) {
    if (!ctx.commands || !mShader) return;
    if (!mFBO) return;                        // RT 还没建好（OnResize 还没被调用过）

    // ---------------------------------------------------------
    // ① 切到 G-Buffer + 设视口
    // ---------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glViewport(0, 0, mWidth, mHeight);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);                      // 透明物体不进 G-Buffer，所以不用混合
    glDisable(GL_CULL_FACE);

    // ---------------------------------------------------------
    // ② 清屏 —— 用 glClearBufferfv，而不是 glClearColor
    //    glClearColor 是【全局状态】，改了会污染下一帧主帧缓冲的 Clear()。
    //    这几个调用直接指定"清哪个附件、清成什么值"，不碰全局状态。
    // ---------------------------------------------------------
    const GLfloat clearNormal[4] = {0.0f, 0.0f, 0.0f, 0.0f};   // 无法线 = 背景
    const GLfloat clearDepth[4]  = {kNoGeometryDepth, 0.0f, 0.0f, 0.0f};  // 哨兵深度 = 背景
    // ★ albedo 的背景清成【黑】而不是白，这是故意选的"失败方向"：
    //   万一日后有人在没检查"这里是不是背景"的情况下采了这张图，
    //   黑色 = 不加光（画面偏暗、容易发现），白色 = 凭空多出一块亮光（正是我们刚修完的那类假亮斑）。
    //   失败要往"暗"的方向失败。
    const GLfloat clearAlbedo[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const GLfloat one = 1.0f;
    glClearBufferfv(GL_COLOR, 0, clearNormal);   // 附件 0：法线
    glClearBufferfv(GL_COLOR, 1, clearDepth);    // 附件 1：世界深度
    glClearBufferfv(GL_COLOR, 2, clearAlbedo);   // 附件 2：albedo
    glClearBufferfv(GL_DEPTH, 0, &one);          // 深度缓冲：1.0 = 最远

    // ---------------------------------------------------------
    // ③ 画一遍不透明物体
    //    复用 IShader::SetMatrix / SetCamera，和别的 Pass 同一套 uniform
    // ---------------------------------------------------------
    mShader->Use();
    mShader->SetCamera(ctx.cameraPos);           // 片元着色器用它算"到相机的距离"

    for (const RenderCommand& cmd : *ctx.commands) {
        if (!cmd.mesh || !cmd.material) continue;

        // TODO(S4): 又一处靠"是否透明"来筛。正确做法还是给 Material 加标志：
        //   透明物体应该走前向（BasePass），不进 G-Buffer。
        if (cmd.material->renderState.blend != BlendMode::Opaque) continue;

        // ★★ 把【材质的固有色】写进 G-Buffer —— 这是 per-object → per-pixel 的那一步。
        //   SSGI 是全屏 Pass，它没有"当前物体"这个概念（每个像素的间接光来自四面八方、
        //   可能来自不同颜色的表面），只能在屏幕空间里按 UV 采这张图。
        //   所以"这个物体是什么颜色"必须先在这里烘成"这个像素是什么颜色"。
        //
        //   两个 Material 共用同一个 IShader 也没问题：值从 material 取，每次 draw 前重设一遍。
        //   ★ 必须放在 Use() 之后（glUniform* 只对当前绑定的 program 生效）。
        mShader->SetVec3("baseColor", cmd.material->baseColor);

        mShader->SetMatrix(TransformToModelMatrix(cmd.transform),
                           ctx.viewMatrix, ctx.projectionMatrix);
        cmd.mesh->Draw();
    }

    // ---------------------------------------------------------
    // ④ 切回主帧缓冲 + 恢复视口
    // ---------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, ctx.fbWidth, ctx.fbHeight);

    // ---------------------------------------------------------
    // ⑤ 把产出写进 ctx
    // ---------------------------------------------------------
    ctx.gbufferNormalTex = mNormalTex;
    ctx.gbufferDepthTex  = mDepthTex;
    ctx.gbufferAlbedoTex = mAlbedoTex;
    ctx.gbufferWidth     = mWidth;
    ctx.gbufferHeight    = mHeight;

    // ---------------------------------------------------------
    // 调试：G-Buffer 的内容是"看不见"的（它不显示在屏幕上），
    // 所以这里给一个开关，把它读回来看数值对不对。
    // 相机始终看向原点 → 猴头就在屏幕中心附近，所以读中心一小块就够。
    // ★ 默认关闭；想检查的时候把它改成 true（只打印一次，不会刷屏）。
    // ---------------------------------------------------------
    static bool s_dumpedOnce = false;
    static constexpr bool kDumpGBuffer = false;
    if (kDumpGBuffer && !s_dumpedOnce) {
        s_dumpedOnce = true;
        const int block = 64;
        const int bx = (mWidth  > block) ? (mWidth  - block) / 2 : 0;
        const int by = (mHeight > block) ? (mHeight - block) / 2 : 0;

        std::vector<float> normals(static_cast<size_t>(block) * block * 4);
        std::vector<float> depths (static_cast<size_t>(block) * block);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, mFBO);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(bx, by, block, block, GL_RGBA, GL_FLOAT, normals.data());
        glReadBuffer(GL_COLOR_ATTACHMENT1);
        glReadPixels(bx, by, block, block, GL_RED,  GL_FLOAT, depths.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        int   geom = 0;
        float dmin = 1e9f, dmax = -1e9f, dsum = 0.0f;
        float nsum[3] = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < block * block; ++i) {
            const float* n = &normals[static_cast<size_t>(i) * 4];
            const float len2 = n[0]*n[0] + n[1]*n[1] + n[2]*n[2];
            if (len2 > 0.25f) {                       // 有法线 → 这里被几何覆盖了
                ++geom;
                nsum[0] += n[0]; nsum[1] += n[1]; nsum[2] += n[2];
                const float d = depths[i];
                if (d < dmin) dmin = d;
                if (d > dmax) dmax = d;
                dsum += d;
            }
        }
        std::cout << "[GBufferPass] RT " << mWidth << "x" << mHeight
                  << "  中心 " << block << "x" << block << " 块: "
                  << "有几何 " << geom << "/" << block*block << " 像素";
        if (geom > 0) {
            std::cout << "  平均法线=(" << nsum[0]/geom << ", " << nsum[1]/geom << ", " << nsum[2]/geom << ")"
                      << "  世界深度 min=" << dmin << " max=" << dmax << " avg=" << dsum/geom;
        }
        std::cout << std::endl;
    }
}
