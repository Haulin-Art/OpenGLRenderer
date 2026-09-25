#pragma once
#include "RenderCommand.h"      // 只需要它（context 里用 RenderCommand）
#include <vector>
#include <glm/glm.hpp>

// ============================================================
// 每帧一次的「帧上下文」
//
// 所有 Pass 的输入从这里拿，Pass 之间的产物也往这里放。
// ★ 关键约定：renderer 每帧创建【一个】ctx，依次传给所有 Pass。
//   Pass 之间不互相认识，只通过 ctx 交换数据。
//
// ★ 设计原则：Pass 不认识 Renderer、不认识别的 Pass、不认识 Camera 类 ——
//   它只认识这个 ctx。这样加/减/重排 Pass 都不会牵动别人。
// ============================================================
struct OpenGLRenderContext {
    // ---- 队列（收集和排序由上层负责，Pass 只消费）----
    const std::vector<RenderCommand>* commands = nullptr;  // 已排好序的渲染队列

    // ---- 主帧缓冲尺寸（px）----
    // ★ 谁改过 glViewport 谁负责恢复，恢复用的就是这两个值，
    //   所以 renderer 每帧必须把它们填对，否则视图会跑到错误区域。
    int fbWidth = 0, fbHeight = 0;

    // ---- 每帧一次的帧数据（renderer 填；等价于架构文档里的 FrameData）----
    glm::mat4 viewMatrix       = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);
    glm::vec3 cameraPos        = glm::vec3(0.0f);
    glm::vec3 lightPos         = glm::vec3(0.0f);
    glm::vec3 lightColor       = glm::vec3(1.0f);

    // ---- 跨 Pass 的产物：写前在的 Pass 填，后面的 Pass 读 ----
    //    具名字段（拼错→编译报错；类型安全），比 string→map 的命名槽位更适合现在这个规模
    glm::mat4    lightSpaceMatrix = glm::mat4(1.0f);   // ShadowPass 写 → BasePass 读
    unsigned int shadowMapTex     = 0;                 // 同上

    // ---- G-Buffer 产物（GBufferPass 写；AO / SSRT / 后处理读）----
    unsigned int gbufferNormalTex = 0;   // 世界法线（RGBA16F，xyz 放法线）
    unsigned int gbufferDepthTex  = 0;   // 线性世界深度（R32F，值 = 到相机的世界距离）
    unsigned int gbufferAlbedoTex = 0;   // ★ 材质的固有色（RGB8）—— SSGI 用它给"命中点"上色
    int          gbufferWidth     = 0;   // G-Buffer 的分辨率（和 fbWidth 一样，但显式写出来更清楚）
    int          gbufferHeight    = 0;

    // ---- 环境光遮蔽（SSAOPass 写 raw；SSAOBlurPass 写 blur；BasePass 读 blur）----
    unsigned int ssaoRawTex = 0;   // 未模糊的 AO（只在调试时看）
    unsigned int ssaoTex    = 0;   // 模糊后的 AO（BasePass 乘进光照）
    int          ssaoWidth  = 0;
    int          ssaoHeight = 0;

    // ---- 屏幕空间阴影（ScreenShadowPass 写 raw；ScreenShadowBlurPass 写 blur；BasePass 读 blur）----
    //   PCSS 从"每个材质着色器里各算一遍"搬到了这里：整屏只算一次，
    //   可以降分辨率、也可以单独做双边模糊去噪。
    unsigned int screenShadowRawTex = 0;   // 未模糊（调试用）
    unsigned int screenShadowTex    = 0;   // 模糊后，BasePass 采样它
    int          screenShadowWidth  = 0;   // 注意：这是【降分辨率后】的尺寸（默认半分辨率）
    int          screenShadowHeight = 0;

    // ---- 屏幕空间全局光照（SSGIPass 写；BasePass 加成间接光）----
    //   ★ 它是【加到环境光上】的一项，不像 AO 只能乘。
    //     关掉 SSGI 时这张纹理被清成 0，BasePass 加 0 → 画面和没有 SSGI 完全一样。
    unsigned int ssgiRawTex = 0;   // SSGIPass 的原始输出（带噪点）
    unsigned int ssgiTex    = 0;   // ★ SSGIBlurPass 去噪后的，BasePass 采样这一张
    int          ssgiWidth  = 0;   // 半分辨率（两张同尺寸）
    int          ssgiHeight = 0;
};

// Pass 的阶段：用来排序。用枚举而不是裸 int，顺序就变成"自文档"
// ★ 数值小的先跑
enum class RenderPassStage : int {
    Shadow           = 100,   // 渲染灯光空间的 shadow map（离屏深度图）
    GBuffer          = 200,   // 输出世界法线 / 世界深度
    AO               = 300,   // 屏幕空间环境光遮蔽（全屏）
    AOBlur           = 310,   // 把 AO 磨平（全屏；必须另起一趟，否则会读到自己正在写的纹理）
    ScreenShadow     = 350,   // 屏幕空间 PCSS（全屏；依赖 G-Buffer + shadow map）
    ScreenShadowBlur = 360,   // 把 PCSS 结果磨平（全屏，同理必须另起一趟）
    SSGI             = 370,   // 屏幕空间全局光照（全屏；★ 必须在 ScreenShadow 之后 —— 命中点的可见性要靠它）
    SSGIBlur         = 371,   // 把 SSGI 去噪（全屏；必须另起一趟，否则会读到自己正在写的纹理）
    Opaques          = 500,
    Transparents     = 600,
    PostProcess      = 800,
};

// ============================================================
// Pass 基类
//
// 三条原则：
//   ① Pass 不认识 Renderer —— 输入全从 Execute(ctx) 进来
//   ② Pass 自己的 GPU 资源自己持有（RAII）：Setup() 建、析构删
//      → 析构必须在 GL 上下文销毁（glfwTerminate）之前跑完
//   ③ 顺序只有一个来源：Stage()
// ============================================================
class OpenGLRenderPass {
    public:
        virtual ~OpenGLRenderPass() = default;

        virtual bool Setup() = 0;                              // 只调一次；false = 初始化失败
        virtual void OnResize(int w, int h) {}                 // 窗口尺寸变化（自己持有屏幕尺寸 RT 的 Pass 才需要）
        virtual void Execute(OpenGLRenderContext& ctx) = 0;     // ★ 非 const：要往 ctx 写跨 Pass 资源
        virtual RenderPassStage Stage() const = 0;             // 排序用
};
