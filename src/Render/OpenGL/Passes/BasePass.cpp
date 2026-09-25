#include "BasePass.h"

// ★ 谁用谁 include：这个 .cpp 直接调 gl* / 用 GL_* 枚举，就必须自己带上 glad，
//   不能指望"基类头里恰好 include 了 glad"（那样一旦基类头瘦身，这里就会莫名其妙报
//   'GLenum does not name a type'）。
#include <glad/glad.h>

// ============================================================
// 文件内部的"状态翻译"工具
//
// ★ 这里刻意【不做缓存】：每次绘制前无条件把状态发出去。
//   原因：GL 状态是全局的，而"缓存"成立的前提是缓存与 GL 真实状态严格一致；
//   ShadowPass 会改深度状态，如果两边各存一份缓存，认知必然互相打架
//   （这个坑你已经踩过两次）。
//
//   正解是把这套东西抽成【共享的】OpenGLStateCache，由 renderer 持有、
//   放进 ctx 让所有 Pass 共用 —— 那样缓存只有一份，才可能保持一致。
//   在那之前，物体这么少，几次多余的 glEnable 完全无感。
// ============================================================
namespace {

GLenum RenderStateToOpenGL(DepthFunc f) {
    switch (f) {
        case DepthFunc::Never:        return GL_NEVER;
        case DepthFunc::Less:         return GL_LESS;
        case DepthFunc::Equal:        return GL_EQUAL;
        case DepthFunc::LessEqual:    return GL_LEQUAL;
        case DepthFunc::Greater:      return GL_GREATER;
        case DepthFunc::NotEqual:     return GL_NOTEQUAL;
        case DepthFunc::GreaterEqual: return GL_GEQUAL;
        case DepthFunc::Always:       return GL_ALWAYS;
    }
    return GL_LESS;
}

GLenum RenderStateToOpenGL(CullMode m) {
    switch (m) {
        case CullMode::Off:   return GL_NONE;
        case CullMode::Front: return GL_FRONT;
        case CullMode::Back:  return GL_BACK;
    }
    return GL_BACK;
}

GLenum RenderStateToOpenGL(BlendMode m) {
    // TODO(R1): 这个函数返回单个值，却被拿去喂 glBlendFunc 的两个参数。
    //   AlphaBlend 恰好正确，Additive / Multiply 是错的。
    //   这里先原样保留（本次是纯重构，不改行为）；修的时候要改成
    //   一个 case 里同时设 glBlendFunc(src, dst)。
    switch (m) {
        case BlendMode::Opaque:     return GL_ONE;
        case BlendMode::AlphaBlend: return GL_SRC_ALPHA;
        case BlendMode::Additive:   return GL_ONE;
        case BlendMode::Multiply:   return GL_ZERO;
    }
    return GL_ONE;
}

void ApplyRenderState(const RenderState& rs) {
    // 深度测试
    if (rs.depthTest) glEnable(GL_DEPTH_TEST);
    else              glDisable(GL_DEPTH_TEST);

    // 深度写入（注意：和"深度测试"是两个独立开关）
    glDepthMask(rs.depthWrite ? GL_TRUE : GL_FALSE);

    // 深度比较
    glDepthFunc(RenderStateToOpenGL(rs.depthFunc));

    // 面剔除
    if (rs.cullMode == CullMode::Off) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(RenderStateToOpenGL(rs.cullMode));
    }

    // 混合
    if (rs.blend == BlendMode::Opaque) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        glBlendFunc(RenderStateToOpenGL(rs.blend), GL_ONE_MINUS_SRC_ALPHA);
    }
}

} // namespace

// ============================================================
// 每帧执行
// ============================================================
void BasePass::Execute(OpenGLRenderContext& ctx) {
    if (!ctx.commands) return;

    // 自己保证视口正确（ShadowPass 也会恢复，这里再设一次：让每个 Pass
    // 都"自带视口正确性"，顺序依赖就不会咬人）
    glViewport(0, 0, ctx.fbWidth, ctx.fbHeight);

    for (const RenderCommand& command : *ctx.commands) {
        if (!command.mesh || !command.material) continue;
        IShader* shader = command.material->GetShader();
        if (!shader) continue;

        // ★ glUniform* 只对【当前绑定的 program】生效 → 所有 Set* 必须在 Use() 之后
        shader->Use();

        // ---- 把【屏幕空间阴影】绑到 0 号纹理单元 ----
        //   ★ 注意材质这边已经不需要知道"灯光矩阵 / 深度图 / PCF / PCSS"了 ——
        //     它只做一次屏幕空间采样。这正是把 PCSS 搬进 ScreenShadowPass 的收益：
        //     材质着色器变简单、且每个屏幕像素只算一次阴影（不管有多少物体、多少重叠）。
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.screenShadowTex);
        shader->SetInt("screenShadow", 0);

        // ---- 把 SSAO 的结果绑到 1 号纹理单元 ----
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ctx.ssaoTex);
        shader->SetInt("aoMap", 1);

        // ---- 把 SSGI 的间接光绑到 2 号纹理单元 ----
        //   ★ 这项是【加到环境光上】的（不像 AO 只能乘）。
        //     关掉 SSGI 时 SSGIPass 会把这张纹理清成 0，所以这里可以无条件地绑、无条件地加 ——
        //     材质这边完全不需要知道 SSGI 开没开。
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, ctx.ssgiTex);
        shader->SetInt("ssgiMap", 2);

        // ---- ★ 屏幕尺寸：屏幕空间纹理的 UV 必须用它当分母 ----
        //   不能用 textureSize(屏幕空间纹理)——那个只有在"纹理是全分辨率"时才等于屏幕尺寸；
        //   阴影纹理是半分辨率的，拿它当分母会让 UV 变成 0~2，
        //   画面就被缩小、贴到左下角（这个坑已经踩过一次）。
        shader->SetVec2("screenSize", glm::vec2(static_cast<float>(ctx.fbWidth),
                                                static_cast<float>(ctx.fbHeight)));
        // 注：地面用的 groundNetFrag 没有声明 aoMap，所以这里设了也是 location=-1，被规范要求忽略
        shader->SetVec3("baseColor",glm::vec3(command.material->baseColor));

        // ---- 应用材质描述的状态 ----
        ApplyRenderState(command.material->renderState);

        // ---- 本物体的数据 ----
        shader->SetMatrix(TransformToModelMatrix(command.transform),
                          ctx.viewMatrix, ctx.projectionMatrix);
        shader->SetLight(ctx.lightPos, ctx.lightColor);
        shader->SetCamera(ctx.cameraPos);

        command.mesh->Draw();
    }

    // ★ 用完解绑：否则下一帧 ShadowPass 把同一张纹理当【深度附件】渲染时，
    //   就形成"同一张纹理既被写、又被采样"的反馈循环（S2）。
    //   1 号单元是 SSAO 的 AO 纹理、2 号是 SSGI 的间接光，下一帧都要被别的 Pass 写，同样要解绑。
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}
