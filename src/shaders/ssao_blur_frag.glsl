#version 460 core

// ============================================================
// SSAO 模糊 —— 单纯做个 5×5 的"深度加权"模糊
//
// ★ 为什么必须有这一趟：
//   SSAO 每个像素只撒 32 个样本，结果一定带噪点；不模糊的话看起来就是一片颗粒。
// ★ 为什么不能和 SSAO 写在同一个 shader 里：
//   模糊要读"邻居的 AO"，而那个 AO 正是这个 Pass 正在写的纹理
//   —— 同一张纹理既读又写 = 反馈循环（未定义行为）。所以必须分成两趟。
// ★ 为什么要带深度权重（而不是普通盒子模糊）：
//   普通模糊会把"物体边缘两边"的 AO 混在一起，物体周围会出现一圈亮边（halo）。
//   用深度差做权重 → 深度差得远的像素权重接近 0 → 只模糊同一个表面上的邻居。
// ============================================================

in vec2 vUV;

out float fragAO;

uniform sampler2D ssaoInput;   // 未模糊的 AO
uniform sampler2D gDepth;      // 世界深度，用来算权重

const int   BLUR_RADIUS  = 2;    // 5x5

// ★ 深度权重的尺度：这个数字的量级很关键，很容易设错。
//   同一个表面上，相邻像素的深度差大概只有 0.05 ~ 0.3（世界单位）；
//   而跨过物体边缘时，深度差会跳到 1.0 以上。
//   所以阈值要落在中间：能把前者当"同一面"融在一起，又能把后者挡掉。
//
//   这里用了【随距离自适应】的尺度（centerZ * 0.02）：
//   远处深度量化更粗，容忍度应该大一点；近处反之。
//   如果写成固定值，比如 25，那么 exp(-0.05*25)=0.29 → 权重几乎全是 0
//   → 等于完全没模糊，看到的还是原始的噪点块。
const float DEPTH_REL    = 0.02;
const float DEPTH_MIN    = 0.05;

void main()
{
    vec2  texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));
    float centerZ   = texture(gDepth, vUV).r;
    float depthScale = max(DEPTH_MIN, centerZ * DEPTH_REL);   // 自适应尺度

    float sum = 0.0;
    float wsum = 0.0;
    for (int y = -BLUR_RADIUS; y <= BLUR_RADIUS; ++y) {
        for (int x = -BLUR_RADIUS; x <= BLUR_RADIUS; ++x) {
            vec2  offset = vec2(float(x), float(y)) * texelSize;
            float z      = texture(gDepth, vUV + offset).r;
            float w      = exp(-abs(z - centerZ) / depthScale);   // 深度越接近，权重越大
            sum  += texture(ssaoInput, vUV + offset).r * w;
            wsum += w;
        }
    }

    fragAO = (wsum > 0.0) ? (sum / wsum) : 1.0;
}
