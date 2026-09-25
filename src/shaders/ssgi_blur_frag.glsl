#version 460 core

// ============================================================
// SSGI 去噪 —— 深度 + 法线加权的双边模糊
//
// ★ 为什么必须有这一趟：
//   SSGI 每像素只撒 24 根射线，每根射线只命中一次 → 估计量方差很大，看起来是一层颗粒。
//   单纯加射线数的收益是 √N（射线翻 4 倍才把噪点减半），代价却线性涨。
//   空间去噪才是性价比最高的一步。
//
// ★ 为什么不能和 SSGI 写在同一个 shader 里：
//   模糊要读"邻居的间接光"，而那张纹理正是 SSGI 这一趟正在写的
//   —— 同一张纹理既读又写 = 反馈循环（未定义行为）。必须分成两趟、两张纹理。
//
// ★ 为什么要带权重（而不是普通盒子模糊）：
//   ① 深度权重：物体轮廓两侧的深度差很大 → 权重趋近 0 → 不会把物体的间接光糊到背景上，
//      也不会把地面的间接光糊到物体边缘（那就是一圈假的"亮边/halo"）。
//   ② 法线权重：弯曲表面上（猴头）相邻像素深度很接近，光靠深度分不出
//      "同一个面"和"隔着一个折角的两个面"，法线能把它们分开。
// ============================================================

in vec2 vUV;

out vec4 fragColor;

uniform sampler2D ssgiInput;   // 未去噪的间接光（半分辨率）
uniform sampler2D gDepth;      // 世界深度（全分辨率，这里当权重用）
uniform sampler2D gNormal;     // 世界法线（全分辨率，这里当权重用）

const int BLUR_RADIUS = 4;      // 9x9。GI 的噪点颗粒比 AO 大，核也要更大

// ★ 深度权重的尺度（和 ssao_blur_frag 同一个道理，量级很关键）：
//   同一个表面上相邻像素的深度差只有 0.05~0.3；跨过轮廓会跳到 1.0 以上。
//   用【随距离自适应】的尺度：远处深度量化更粗，容忍度该大一点。
const float DEPTH_REL = 0.02;
const float DEPTH_MIN = 0.05;

void main()
{
    vec2  texel     = 1.0 / vec2(textureSize(ssgiInput, 0));
    float centerZ   = texture(gDepth, vUV).r;
    vec3  centerN   = normalize(texture(gNormal, vUV).xyz + vec3(1e-5));   // 防 normalize(0)
    float depthScale = max(DEPTH_MIN, centerZ * DEPTH_REL);

    vec3  sum  = vec3(0.0);
    float wsum = 0.0;

    for (int y = -BLUR_RADIUS; y <= BLUR_RADIUS; ++y) {
        for (int x = -BLUR_RADIUS; x <= BLUR_RADIUS; ++x) {
            vec2  uv = vUV + vec2(float(x), float(y)) * texel;

            float z  = texture(gDepth,  uv).r;
            vec3  n  = texture(gNormal, uv).xyz;

            float depthW  = exp(-abs(z - centerZ) / depthScale);          // 深度越接近权重越大
            float nd      = max(dot(normalize(n + vec3(1e-5)), centerN), 0.0);
            float normalW = nd * nd * nd * nd;                             // 法线越同向权重越大

            float w = depthW * normalW;
            sum  += texture(ssgiInput, uv).rgb * w;
            wsum += w;
        }
    }

    fragColor = vec4((wsum > 0.0) ? (sum / wsum) : vec3(0.0), 1.0);
}
