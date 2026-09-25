#version 460 core

// ============================================================
// 屏幕空间阴影的双边模糊（和 SSAO 那个是同一个算法，只是输入不同）
//
// ★ 为什么要模糊：
//   PCSS 的第一步（blocker search）用 16 个点去估"遮挡物平均深度"，
//   这个估计是抖的 → 半影半径在相邻像素之间跳变 → 阴影边缘出现斑驳。
//
// ★ 为什么按深度加权（双边）：
//   普通模糊会把物体轮廓两侧的阴影值混在一起，物体边缘凭空多出一条假的亮边/暗边。
//
// ★ 为什么不能和 PCSS 算在同一趟里：
//   模糊要读邻居的阴影值，而那正是本 Pass 要写的纹理 → 反馈循环。必须分两趟。
// ============================================================

in vec2 vUV;

out float fragShadow;

uniform sampler2D blurInput;   // 未模糊的屏幕空间阴影
uniform sampler2D gDepth;      // 世界深度，用来算权重

const int   BLUR_RADIUS = 2;     // 5x5
const float DEPTH_REL   = 0.02;  // 深度权重的自适应尺度（同 SSAO 那个，量级很关键）
const float DEPTH_MIN   = 0.05;

void main()
{
    vec2  texelSize  = 1.0 / vec2(textureSize(blurInput, 0));
    float centerZ    = texture(gDepth, vUV).r;
    float depthScale = max(DEPTH_MIN, centerZ * DEPTH_REL);

    float sum  = 0.0;
    float wsum = 0.0;
    for (int y = -BLUR_RADIUS; y <= BLUR_RADIUS; ++y) {
        for (int x = -BLUR_RADIUS; x <= BLUR_RADIUS; ++x) {
            vec2  offset = vec2(float(x), float(y)) * texelSize;
            float z      = texture(gDepth, vUV + offset).r;
            float w      = exp(-abs(z - centerZ) / depthScale);
            sum  += texture(blurInput, vUV + offset).r * w;
            wsum += w;
        }
    }

    fragShadow = (wsum > 0.0) ? (sum / wsum) : 1.0;
}
