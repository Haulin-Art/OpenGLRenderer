#version 460 core

// ============================================================
// 屏幕空间 PCSS
//
// 以前 PCSS 写在 basicfrag 里 → 每个"材质像素"都要重跑一遍 32 次采样；
// 物体一多、被重复绘制的地方一多，开销就爆了。
//
// 现在改成：对【屏幕上每个可见像素】只算一次，把结果存进一张纹理，
// 材质那边只做一次采样。带来的好处：
//   ① 每个屏幕像素只算一次（不管场景里有多少物体、多少重叠）
//   ② 可以降分辨率算（半分辨率 → 省 4 倍）
//   ③ 可以单独做双边模糊去噪（PCSS 的 blocker search 是有噪声的）
//
// ★ 输入是 G-Buffer：世界法线 + 世界深度
//   → 重建世界坐标（和 SSAO 完全相同的三步），再投影到灯光空间去比深度。
// ============================================================

in vec2 vUV;

out float fragShadow;

uniform sampler2D gNormal;          // G-Buffer：世界法线
uniform sampler2D gDepth;           // G-Buffer：世界深度（到相机的距离）
uniform sampler2D shadowMap;        // 灯光空间的深度图（由 ShadowPass 产出）

uniform mat4 lightSpaceMatrix;      // = lightProjection * lightView
uniform mat4 ViewMatrix;
uniform mat4 InvProjection;         // 用来重建世界坐标
uniform vec3 CameraPos;

// ---------------- PCSS 参数（从 basicfrag 原样搬过来）----------------
#define PCF_SAMPLES 16

const float kPCSSBias        = 0.002;
const float kPCSSSearchTexel = 10.0;
const float kPCSSLightTexel  = 5.0;
const float kPCSSBlockerEps  = 0.02;

const float FAR_DIST         = 99.0;   // 超过它就当"背景"（G-Buffer 的清屏哨兵是 100）

// 16 个点，分布在一个「单位圆盘」内
const vec2 kPoissonDisk[PCF_SAMPLES] = vec2[PCF_SAMPLES](
    vec2(-0.94201624, -0.39906216),
    vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870),
    vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845),
    vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554),
    vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507),
    vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367),
    vec2( 0.14383161, -0.14100790)
);

float ShadowTap(vec2 uv, float fragDepth, float bias) {
    float closest = texture(shadowMap, uv).r;
    return (fragDepth - bias) > closest ? 0.0 : 1.0;
}

// ① blocker search：搜索圈里遮挡物的平均深度（负数 = 没有遮挡物）
float FindBlockerDepth(vec2 uv, float receiverDepth, float radiusTexel, float selfEps) {
    vec2  texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float sum   = 0.0;
    int   count = 0;
    for (int i = 0; i < PCF_SAMPLES; ++i) {
        float d = texture(shadowMap, uv + kPoissonDisk[i] * radiusTexel * texelSize).r;
        if (d < receiverDepth - selfEps) { sum += d; ++count; }
    }
    return (count > 0) ? sum / float(count) : -1.0;
}

// ② 半影大小
float PenumbraSize(float receiverDepth, float blockerDepth) {
    return (receiverDepth - blockerDepth) / blockerDepth;
}

// ③ 可变半径 PCF
float ShadowFactorPCSS(vec3 worldPos, float bias, float searchTexel, float lightTexel) {
    vec4 lp   = lightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 proj = lp.xyz / lp.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 1.0;

    float blocker = FindBlockerDepth(proj.xy, proj.z, searchTexel, kPCSSBlockerEps);
    if (blocker < 0.0) return 1.0;

    float radius = clamp(PenumbraSize(proj.z, blocker) * lightTexel, 1.0, searchTexel);

    vec2  texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float lit = 0.0;
    for (int i = 0; i < PCF_SAMPLES; ++i) {
        vec2 uv = proj.xy + kPoissonDisk[i] * radius * texelSize;
        lit += ShadowTap(uv, proj.z, bias);
    }
    return lit / float(PCF_SAMPLES);
}

void main()
{
    vec3  N    = texture(gNormal, vUV).xyz;
    float dist = texture(gDepth,  vUV).r;

    // 背景 / 没有几何的像素 → 不产生阴影
    if (dot(N, N) < 0.1 || dist >= FAR_DIST) {
        fragShadow = 1.0;
        return;
    }

    // ------------------------------------------------------------------
    // 重建这个像素的世界坐标（和 SSAO 里一模一样的三步）
    //   UV → NDC → InvProjection 得视空间方向 → 转世界方向 → 乘距离
    // ------------------------------------------------------------------
    vec2 ndc   = vUV * 2.0 - 1.0;
    vec4 viewH = InvProjection * vec4(ndc, 1.0, 1.0);
    vec3 viewDir  = normalize(viewH.xyz / viewH.w);
    vec3 worldDir = normalize(transpose(mat3(ViewMatrix)) * viewDir);
    vec3 worldPos = CameraPos + worldDir * dist;

    fragShadow = ShadowFactorPCSS(worldPos, kPCSSBias, kPCSSSearchTexel, kPCSSLightTexel);
}
