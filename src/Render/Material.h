#pragma once

#include "Shader.h"

#include <tuple>
enum class DepthFunc { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class CullMode  { Off, Front, Back };
enum class BlendMode {
    Opaque,       // 不混合        → glDisable(GL_BLEND)
    AlphaBlend,   // 普通半透明    → glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
    Additive,     // 加法混合      → glBlendFunc(GL_SRC_ALPHA, GL_ONE)
    Multiply,     // 乘法混合      → glBlendFunc(GL_DST_COLOR, GL_ZERO)
};

struct RenderState {
    bool depthTest = true;
    bool depthWrite = true;
    DepthFunc depthFunc  = DepthFunc::Less;
    CullMode  cullMode   = CullMode::Back;
    BlendMode blend      = BlendMode::Opaque;
    // 排序依据
    bool operator<(const RenderState& o) const {
        return std::tie(depthTest, depthWrite, depthFunc, cullMode, blend)
             < std::tie(o.depthTest, o.depthWrite, o.depthFunc, o.cullMode, o.blend);
    }
};


class Material {
    public:
        Material(Shader* shader);
        ~Material();

        void SetShader(Shader* shader);
        Shader* GetShader() const { return m_Shader; }
        RenderState RenderState;


    private:
        Shader* m_Shader = nullptr;
};