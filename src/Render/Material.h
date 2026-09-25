#pragma once

#include "IShader.h"

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
    bool depthTest = false;
    bool depthWrite = true;
    DepthFunc depthFunc  = DepthFunc::Less;
    CullMode  cullMode   = CullMode::Off;
    BlendMode blend      = BlendMode::Opaque;
    // 排序依据
    // 排序要做到两点，一是半透明必须排在所有不透明之后，二是 同 shader / 同状态 的物体连在一起画。
    // 优先级：是否透明 》 shader分组 》渲染状态分组 》距离
    bool operator<(const RenderState& o) const {
        return std::tie(depthTest, depthWrite, depthFunc, cullMode, blend)
             < std::tie(o.depthTest, o.depthWrite, o.depthFunc, o.cullMode, o.blend);
    }
};

class Material {
    public:
        Material(IShader* shader);
        ~Material();

        void SetShader(IShader* shader);
        IShader* GetShader() const { return m_Shader; }
        RenderState renderState;

    private:
        IShader* m_Shader = nullptr;
};