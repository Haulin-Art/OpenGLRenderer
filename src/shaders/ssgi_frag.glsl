#version 460 core

// ============================================================
// SSGI —— 屏幕空间全局光照（单次弹射 / single bounce）
//
// 输入：G-Buffer（世界法线 + 世界深度）
//       屏幕空间阴影（ScreenShadowBlurPass 那张，已模糊）→ 判断"命中点有没有被太阳照到"
// 输出：RGB 间接光（半分辨率、HDR）
//
// ★ 和 SSAO 的本质区别（这是理解 SSGI 的关键）：
//     SSAO 只问【这根射线被挡住没有】           → 得到 [0,1] 的标量 → 只能把环境光调暗。
//     SSGI 进一步问【挡住它的那个表面自己有多亮】 → 得到颜色/亮度     → 能把光搬过来。
//   所以 AO 的数学性质是"只能减光"，而 SSGI 可以【加光】—— 这才是 GI。
//
// ============================================================
// 第一版有 3 类问题，下面每一处修复都对应一条：
//
//   【错误亮斑 ①】命中判定没有上界。
//       "采样点跑到某像素几何后面"就算命中，但没限制"跑到后面多远"。
//       射线贴着轮廓擦过去时，uv_s 会突然跳到一块【离相机近得多】的几何上，
//       diff 一下变成几十 → 却照样被当成命中，而且 t 很小 → 衰减 1/(1+t²)≈1
//       → 一个巨大的假贡献。这就是"平白无故冒出来的亮斑"。
//       ✅ 修复：加【厚度上界】diff < thickness，超过就只是"跳过了轮廓"，继续走。
//          （SSAO 早就有对应的保护 —— rangeCheck；我这里漏了。）
//
//   【错误亮斑 ②】没有背面剔除。
//       命中面的正面对着光、却背对着接收者时，它的辐射【不会朝我们发出】。
//       第一版只判了"它被光照到没有"，没判"它的正面朝着我们没有"。
//       ✅ 修复：命中面的法线与"命中点→接收者"方向必须同向，否则它只遮挡、不发光。
//          （这也是 SSAO 的 normalCheck 在做的事。）
//
//   【噪点】命中距离被量化。
//       步进只有 14 步，t 只能取 14 个离散值 → 衰减 1/(1+t²) 也只有 14 档跳变；
//       而且"这一步到底算不算命中"还会随步的相位翻来覆去。
//       这是结构性噪点，光加射线数是治不好的。
//       ✅ 修复：把命中位置在"上一步/这一步"之间【线性插值】，求出真正穿过的那一点。
//          顺带解决"薄物体被跨过去"的穿透问题。
// ============================================================

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D gNormal;       // 世界法线（RGB）
uniform sampler2D gDepth;        // 世界深度 = 到相机的距离
uniform sampler2D gAlbedo;       // ★ 命中点的材质固有色 —— "颜色渗透"就靠它
uniform sampler2D screenShadow;  // 屏幕空间阴影（已模糊）：1 = 被照亮

uniform mat4 ViewMatrix;
uniform mat4 Projection;
uniform mat4 InvProjection;      // = inverse(Projection)
uniform vec3 CameraPos;
uniform vec3 mainLightPos;
uniform vec3 mainLightColor;

// ---- 可调参数 ----
const int   RAY_COUNT   = 24;    // 每像素射线数
const int   STEP_COUNT  = 24;    // 每根射线的步进数
const float RAY_LENGTH  = 3.5;   // 射线最远走多远（世界单位）
const float kMinStep    = 0.15;  // 第一步至少走多远（躲开近场自交）
const float kOriginLift  = 0.04; // ★ 射线起点沿法线抬起这么多（世界单位）
const float FAR_DIST    = 90.0;  // 超过它当作背景（G-Buffer 的清屏哨兵是 100）
const float kIntensity  = 1.0;   // 总强度（余弦加权的估计量已经等价于 E/π）
const vec3  kMaxIndirect = vec3(1.5);   // 限幅：抑制个别射线造成的亮度尖点（firefly）

// 厚度上界的下限（世界单位）。真正的上界按【当前步长】自适应，见下面 ③。
const float kThicknessMin = 0.05;
const float kThicknessStepScale = 2.0;  // 允许"一步之内"跨过的最多深度差

// ------------------------------------------------------------
// 余弦加权半球采样
//
// ★ 为什么用"余弦加权"而不是均匀采样：
//   我们要算的是 E = ∫ L(ω)·cosθ dω。如果按 pdf = cosθ/π 采样，
//   估计量就变成 (1/N)Σ L·cosθ/pdf = (1/N)Σ L·π —— cosθ 被消掉了。
//   也就是说【接收者的 N·L 项不用显式乘】，采样本身已经包含了它，
//   而且方向自然集中在法线附近（近处的光更影响大）。
// ------------------------------------------------------------
vec3 CosineHemisphere(float u1, float u2)
{
    float phi = 2.0 * 3.14159265 * u1;
    float r   = sqrt(u2);
    return vec3(cos(phi) * r, sin(phi) * r, sqrt(max(0.0, 1.0 - u2)));
}

// UV → 随机数，用来给每个像素一个不同的旋转角（把规律性花纹打散成噪点）
float Hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main()
{
    float dist = texture(gDepth, vUV).r;
    vec3  N    = texture(gNormal, vUV).xyz;

    // ---- 背景 / 没有几何：没有间接光 ----
    if (dot(N, N) < 0.1 || dist >= FAR_DIST) {
        fragColor = vec4(0.0);
        return;
    }
    N = normalize(N);

    // ---- ① 重建世界位置（和 ssao_frag 完全同一套推导）----
    vec2 ndc   = vUV * 2.0 - 1.0;
    vec4 viewH = InvProjection * vec4(ndc, 1.0, 1.0);
    vec3 viewDir  = normalize(viewH.xyz / viewH.w);
    vec3 worldDir = normalize(transpose(mat3(ViewMatrix)) * viewDir);
    vec3 worldPos = CameraPos + worldDir * dist;

    // ---- ② 围绕法线的 TBN + 每像素随机旋转 ----
    vec3 up = (abs(N.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 T  = normalize(cross(up, N));
    vec3 B  = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    vec2  texSize = vec2(textureSize(gDepth, 0));
    float jitter  = Hash21(floor(vUV * texSize));   // 每像素一个 [0,1) 的偏移

    vec3  L = normalize(mainLightPos);              // 和基础着色器同一个约定（光源离原点很近时成立）

    // ★ 射线起点沿法线抬起一点。
    //   这一步很关键：不抬的话，射线一出门就在表面上，掠射角下"自己撞到自己"
    //   的符号会变成随机的（深度精度决定的）→ 地面上冒出亮斑。
    //   抬起之后，采样点永远在表面【靠近相机的一侧】→ 它到相机的距离比表面小 → 不会自交。
    vec3  rayOrigin = worldPos + N * kOriginLift;
    float bias      = max(0.02, dist * 0.002);      // 深度精度容差

    vec3 indirect = vec3(0.0);

    for (int i = 0; i < RAY_COUNT; ++i) {
        // 低差异序列：u1 均匀铺开，u2 用黄金比 + 每像素抖动
        float u1 = (float(i) + 0.5) / float(RAY_COUNT);
        float u2 = fract(float(i) * 0.6180339887 + jitter);
        vec3  dir = TBN * CosineHemisphere(fract(u1 + jitter), u2);

        // ---- ③ 屏幕空间步进 ----
        //   prevT/prevDiff 记住上一步的状态，用来做命中插值。
        //   prevDiff 初始给个负数 = "上一瞬间还在表面前面"。
        float prevT    = 0.0;
        float prevDiff = -1.0;

        for (int s = 1; s <= STEP_COUNT; ++s) {
            // ★ 近密远疏：u² 让步长随距离线性增长。
            //   近处的反弹（接触遮蔽、缝隙漏光）才是最有信息量的，远处本来就该糊。
            float u = float(s) / float(STEP_COUNT);
            float t = kMinStep + (RAY_LENGTH - kMinStep) * u * u;
            vec3  S = rayOrigin + dir * t;

            // 把采样点投影回屏幕
            vec4 clip = Projection * (ViewMatrix * vec4(S, 1.0));
            if (clip.w <= 0.0) break;                              // 跑到相机后面了 → 这条射线作废
            vec3 ndcS = clip.xyz / clip.w;
            if (abs(ndcS.x) > 1.0 || abs(ndcS.y) > 1.0) break;      // 出屏幕 → 屏幕空间没数据了
            vec2 suv = ndcS.xy * 0.5 + 0.5;

            float sceneD = texture(gDepth, suv).r;
            if (sceneD >= FAR_DIST) {                               // 那个像素是背景 → 没有东西可撞
                prevT = t; prevDiff = -1.0;
                continue;
            }

            float sampleD = distance(S, CameraPos);
            float diff    = sampleD - sceneD;                       // >0 表示采样点钻到了那块几何后面

            // ★★ 厚度上界：真正的"穿过"一定发生在【一步之内】，
            //    所以允许的深度差应该和当前步长同量级。
            //    如果 diff 远超一步的能量，说明 uv_s 跳到了轮廓另一侧的远物体上
            //    —— 那不是命中，只是擦过了轮廓，继续走。
            const float stepLen   = t - prevT;
            const float thickness = max(stepLen * kThicknessStepScale, kThicknessMin) + bias;

            if (diff <= bias) {                                     // 还在前面（或误差范围内）→ 继续
                prevT = t; prevDiff = diff;
                continue;
            }
            if (diff >= thickness) {                                // 跨过了轮廓，不是命中 → 继续
                prevT = t; prevDiff = -1.0;
                continue;
            }

            // ---- ④ 命中：uv_s 上那块几何挡住了这根射线 ----
            //   ★ 命中位置在"上一步"和"这一步"之间，线性插值求出真正穿过的点。
            //     不做插值的话 hitT 只能取到 24 个离散值 → 衰减跳变 → 结构性噪点。
            float w    = (prevDiff < 0.0) ? (prevDiff / (prevDiff - diff)) : 0.0;
            float hitT = mix(prevT, t, clamp(w, 0.0, 1.0));

            vec3 rawNh = texture(gNormal, suv).xyz;
            vec3 Nh    = (dot(rawNh, rawNh) > 0.01) ? normalize(rawNh) : vec3(0.0);

            // ★★ 背面剔除：命中面必须"正面对着接收者"，否则它的光不会朝我们发出来，
            //    它只是【遮挡】。所以这里只遮挡、不发光。
            //    （-dir 就是"命中点 → 接收者"的方向）
            if (dot(Nh, -dir) <= 0.0) break;

            float lambertH = max(0.0, dot(Nh, L));                  // 命中点自己有没有被太阳照到
            float visibleH = texture(screenShadow, suv).r;          // 命中点有没有被挡住

            // ★★ 命中点的 albedo —— 这就是"颜色渗透（color bleeding）"的全部来源。
            //   没有它的时候 radiance 只有亮度（场景里所有材质都是白的）；
            //   有了它，红色表面反弹出来的光就是红的。
            //
            //   ★ 采样位置是 suv 而不是 vUV：我们要的是【命中点那块表面】的颜色，
            //     不是当前像素的。suv 正是"射线撞上的那个屏幕像素"，
            //     而步进判定保证那里就是命中点本身（sampleDist ≈ sceneDist）。
            //
            //   ★ 只乘命中点的 albedo，不乘接收者的 —— 接收者那边由 BasePass 负责
            //     （fragColor = albedo * direct + ambient * ao）。乘两次会让颜色变暗、发闷。
            vec3 albedoH = texture(gAlbedo, suv).rgb;

            // 命中点的出射辐射 = 它的固有色 × 打在它身上的直接光
            vec3 radiance = albedoH * mainLightColor * lambertH * visibleH;

            // 几何衰减：真正的 1/r² 在这个尺度下衰减太快、GI 会几乎看不见，
            // 这里用 1/(1+r²)（r=1 → 0.5, r=2 → 0.2, r=3.5 → 0.075），更接近"局部反弹"的观感
            float falloff = 1.0 / (1.0 + hitT * hitT);

            indirect += radiance * falloff;
            break;                                                  // 第一次命中就停（单次弹射）
        }
    }

    // ---- ⑤ 归一化 ----
    //   余弦加权的估计量正好是 (1/N)Σ L·π，而"出射辐射" = E/π（albedo=1），
    //   所以除以射线数就完了 —— 不需要再乘 π，也不需要再乘 N·L。
    vec3 result = indirect / float(RAY_COUNT) * kIntensity;
    fragColor = vec4(min(result, kMaxIndirect), 1.0);
}
