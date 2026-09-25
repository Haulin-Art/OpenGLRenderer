#version 460 core

in vec3 vertexNormal; // 从顶点着色器传入的法线变量
in vec3 posWS;

uniform vec3 mainLightPos; // 主光源位置
uniform vec3 mainLightColor; // 主光源颜色

uniform sampler2D shadowMap;                       // 新增
uniform mat4      lightSpaceMatrix;                // 新增

out vec4 fragColor; // 输出到帧缓冲的颜色变量

float ShadowFactor(vec3 worldPos) {
    vec4 lp = lightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 proj = lp.xyz / lp.w;
    proj = proj * 0.5 + 0.5;                       // ★ NDC[-1,1] → UV[0,1]
    if (proj.z > 1.0) return 1.0;                  // 超出灯光视锥 → 不在阴影

    float closest = texture(shadowMap, proj.xy).r;
    float bias = 0.005;
    return (proj.z - bias) > closest ? 0.0 : 1.0;
}


// ---------------- PCF：泊松盘采样 ----------------
#define PCF_SAMPLES 16
// 16 个点，分布在一个「单位圆盘」内（每个点的长度 ≤ 1）
// ★ 必须是 const，且大小是编译期常量 —— 这样编译器才能把循环展开
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
// 单点比较：1.0 = 被照亮，0.0 = 在阴影里（和你原来的语义一致）
float ShadowTap(vec2 uv, float fragDepth, float bias) {
    float closest = texture(shadowMap, uv).r;
    return (fragDepth - bias) > closest ? 0.0 : 1.0;
}
// PCF：在泊松盘内采 N 次取平均 → 连续的 0~1 阴影因子
//   bias       : 深度偏移（治 shadow acne）
//   radiusTexel: 采样半径，单位是【shadow map 的纹素】
float ShadowFactorPCF(vec3 worldPos, float bias, float radiusTexel) {
    vec4 lp = lightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 proj = lp.xyz / lp.w;
    proj = proj * 0.5 + 0.5;                       // NDC[-1,1] → UV[0,1]
    if (proj.z > 1.0) return 1.0;                  // 超出灯光远裁剪面 → 不在阴影
    // ★ 不要硬编码 1024：用 textureSize 反推，改分辨率时不用两处一起改
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float lit = 0.0;
    for (int i = 0; i < PCF_SAMPLES; ++i) {
        vec2 uv = proj.xy + kPoissonDisk[i] * radiusTexel * texelSize;
        lit += ShadowTap(uv, proj.z, bias);
    }
    return lit / float(PCF_SAMPLES);
}


void main()
{
    float lambert = max(0.0,dot(vertexNormal, normalize(mainLightPos)));
    //lambert = lambert * 0.5 + 0.5;
    vec3 diffuse = mainLightColor * lambert;

    float shadow = ShadowFactor(posWS);

    float shadowPCF = ShadowFactorPCF(posWS, 0.002, 2.0);   // ← PCF 版本

    fragColor = vec4(diffuse*shadowPCF*0.8+0.2, 1.0); // 将顶点颜色传递给帧缓冲
    //fragColor = vec4(vec3(shadow), 1.0); // 将顶点颜色传递给帧缓冲
}