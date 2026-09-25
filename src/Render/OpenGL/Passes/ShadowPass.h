#pragma once
#include "OpenGLRenderPass.h"   // 基类 + 帧上下文

#include <memory>
#include <glm/glm.hpp>

class OpenGLShader;             // ★ 前向声明：头文件不需要认识它的内部实现
                                //   （这样 ShadowPass.h 就不会把 glad 拖给每个包含它的文件）

// ============================================================
// ShadowPass —— 从「灯光的角度」渲染一张深度图（shadow map）
//
// 四步（对应原来散在 OpenGLRenderer::ExecuteRenderCommands 里的那段）：
//   ① 切到 shadow RT + 设视口
//   ② 清深度（要写深度、关混合/剔除）
//   ③ 用深度专用着色器画所有「投影者」
//   ④ 切回主帧缓冲 + 恢复视口，并把产出写进 ctx
//
// ★ 资源全部自己持有（RAII）：FBO / 深度纹理 / 深度着色器都是它的成员
//     建 → Setup()      删 → 析构函数
//   所以它必须在 GL 上下文销毁（glfwTerminate）之前被析构
//   —— renderer 用 unique_ptr 持有它，只要 mPasses.clear() 在 terminate 之前就自动满足。
//
// ★ 产出（写进 ctx，给后面的 Pass 用）：
//     ctx.shadowMapTex      深度纹理 id
//     ctx.lightSpaceMatrix  = lightProjection * lightView
//
// ★ 光源参数【不由本 Pass 持有】：光源是场景数据，renderer 每帧把它填进
//   ctx.lightPos / ctx.lightColor，本 Pass 只负责用它算出灯光空间的 V/P。
//   （如果这里再存一份，就有两个真相源了。）
// ============================================================
class ShadowPass final : public OpenGLRenderPass {
public:
    ShadowPass() = default;
    ~ShadowPass() override;                  // ★ 释放 FBO / 深度纹理 / 深度着色器

    // ---- OpenGLRenderPass 接口 ----
    bool Setup() override;                   // 建 RT
    void Execute(OpenGLRenderContext& ctx) override;
    RenderPassStage Stage() const override { return RenderPassStage::Shadow; }

private:
    static constexpr int   kMapSize       = 1024;   // 阴影图分辨率
    static constexpr float kOrthoHalfSize = 5.0f;   // 灯光正交盒的半径（必须恰好包住要投影的场景）

    // 从 ctx.lightPos 算出灯光空间的 V / P（每帧一次，很便宜）
    void UpdateLightMatrices(const glm::vec3& lightPos);

    bool BuildShadowMap();                   // 建 FBO + 深度纹理；失败返回 false
    void DestroyShadowMap();                 // 删 FBO + 深度纹理

    // ---- 本 Pass 拥有的资源 ----
    unsigned int mFBO      = 0;
    unsigned int mDepthTex = 0;
    std::unique_ptr<OpenGLShader> mDepthShader;

    // ---- 灯光空间矩阵（每帧从 ctx.lightPos 重算）----
    glm::mat4 mLightProjection = glm::mat4(1.0f);
    glm::mat4 mLightView       = glm::mat4(1.0f);
};
