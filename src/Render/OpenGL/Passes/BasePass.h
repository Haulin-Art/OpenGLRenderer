#pragma once
#include "OpenGLRenderPass.h"

// ============================================================
// BasePass —— 用材质把队列里的物体正常画一遍（并用 shadow map 算阴影）
//
// ★ 它【没有任何自己的 GPU 资源】：
//     shader / mesh 都来自渲染队列里的 Material，
//     相机 / 光源 / shadow map 全部从 ctx 拿。
//   所以这个类没有成员变量、也没有 setter —— 这就是"用 ctx 交换数据"的好处：
//   Pass 之间零耦合，加减/重排 Pass 都不用改别的 Pass。
//
// 运行前提（由 renderer 保证）：
//   ctx.commands / fbWidth / fbHeight / viewMatrix / projectionMatrix /
//   cameraPos / lightPos / lightColor / lightSpaceMatrix / shadowMapTex
//   都必须在本 Pass 跑之前填好 —— 其中 lightSpaceMatrix 和 shadowMapTex
//   是 ShadowPass 在它自己的 Execute 里写进 ctx 的，所以本 Pass 必须排在它之后
//   （这就是 Stage() 的作用）。
// ============================================================
class BasePass final : public OpenGLRenderPass {
public:
    BasePass() = default;
    ~BasePass() override = default;          // 没有自己的资源要释放

    // ---- OpenGLRenderPass 接口 ----
    bool Setup() override { return true; }   // 没有要建的东西
    void Execute(OpenGLRenderContext& ctx) override;
    RenderPassStage Stage() const override { return RenderPassStage::Opaques; }
};
