#version 460 core

// ============================================================
// SSAO —— 屏幕空间环境光遮蔽（世界空间版）
//
// 输入：G-Buffer 的世界法线 + 世界深度（到相机的距离）
// 输出：单通道 AO（1 = 完全没被遮挡，0 = 完全被遮挡）
//
// 思路（三步）：
//   ① 重建当前像素的世界位置
//   ② 在法线周围的半球里撒 N 个采样点
//   ③ 把每个采样点投影回屏幕，看它是不是"跑到场景几何后面去了"
//
// ★ 为什么遮挡判定可以简化成"比距离"：
//   采样点 S 投影到屏幕上会落在某个 UV；那个 UV 上的几何点 P，一定和 S 在同一条视线上！
//   所以 S 是否被挡住 ⟺ S 到相机的距离 > P 到相机的距离（+偏移）。
//   而"到相机的距离"正是 G-Buffer 里存的东西 —— 不需要比 z，也不需要转换到视空间。
// ============================================================

in vec2 vUV;

out float fragAO;

uniform sampler2D gNormal;   // 世界法线（RGB）
uniform sampler2D gDepth;    // 世界深度 = 到相机的距离

uniform mat4 ViewMatrix;
uniform mat4 Projection;
uniform mat4 InvProjection;  // = inverse(Projection)，用来算每个像素的视线方向
uniform vec3 CameraPos;

const int   KERNEL_SIZE = 32;
const float RADIUS      = 0.6;    // 采样半球半径（世界单位）
const float BIAS        = 0.03;   // 深度偏移，防止自遮挡（AO 里的 "acne"）
const float BIAS_REL    = 0.004;  // 偏移随距离增长的斜率（见下面 bias 的说明）
const float FAR_DIST    = 90.0;   // 超过这个距离当作"背景"（清屏哨兵是 100）

// 随距离淡出：SSAO 在远处本来就不可靠 —— 固定的世界半径投影到屏幕上只剩几个像素，
// 采样的空间分辨率不够，而且远处的间接光本来也不该靠屏幕空间来管。
const float FADE_START  = 15.0;   // 相机距离超过它开始淡出
const float FADE_END    = 45.0;   // 到这里完全不再施加 AO

// normal check 的阈值：dot(遮挡物的法线, 接收者的法线) 低于它 → 完全不算遮挡
//   +1 = 两面完全同向（同一个平面）
//    0 = 两面互相垂直（★ 真实的墙角/凹角就是这样，必须保留它的 AO）
//   -1 = 完全背对背（采样点穿过去打到了背面 → 假遮挡）
// 所以这个值要取【负数】，而且要留一点余量：法线是插值出来的，墙角处不会精确等于 0。
const float kNormalMin  = -0.25;

// ------------------------------------------------------------
// 程序化生成半球采样点（黄金角螺旋）
//
// 为什么不硬编码一堆积数：这些点必须满足两个性质 ——
//   ① 均匀分布在半球上（低差异 → 少噪点）
//   ② 越靠近原点越密（AO 的常规做法：近处的遮挡更重要）
// 黄金角螺旋用 3 行代码就能同时满足，而且分布是确定的、可复现的。
// （真实引擎通常在 CPU 生成一次、用 uniform 数组上传，避免逐像素算；我这里样本数固定
//  且是常量，编译器会把整个循环展开、把这 32 个点直接算成常量，开销可以忽略。）
// ------------------------------------------------------------
vec3 HemisphereSample(int i)
{
    float t     = (float(i) + 0.5) / float(KERNEL_SIZE);   // 0..1
    float r     = sqrt(t);                                  // 半径：面积均匀（不是半径均匀）
    float theta = float(i) * 2.39996323;                    // 黄金角，让点螺旋展开
    float z     = sqrt(max(0.0, 1.0 - r * r));              // 半球：z ≥ 0（+z 就是法线方向）
    float scale = mix(0.15, 1.0, t * t);                    // 让样本更贴近原点
    return vec3(cos(theta) * r, sin(theta) * r, z) * scale;
}

// 用 UV 做 hash → 每像素一个随机的旋转角（避免出现规则的条纹状条纹）
// 量化到 4×4 块，模仿"噪声纹理"的常见做法
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

    // ---- 背景 / 没有几何的像素：完全不遮挡 ----
    if (dot(N, N) < 0.1 || dist >= FAR_DIST) {
        fragAO = 1.0;
        return;
    }
    N = normalize(N);

    // ---------------------------------------------------------
    // ① 重建这个像素的世界位置
    //    屏幕 UV → NDC → 用 InvProjection 拿到"这条视线在远平面上的点"
    //    → 归一化就是视空间方向；再转到世界空间（视图矩阵是正交的，转置就是逆）
    //    → 世界位置 = 相机位置 + 世界方向 × 距离
    // ---------------------------------------------------------
    vec2 ndc   = vUV * 2.0 - 1.0;
    vec4 viewH = InvProjection * vec4(ndc, 1.0, 1.0);
    vec3 viewDir  = normalize(viewH.xyz / viewH.w);
    vec3 worldDir = normalize(transpose(mat3(ViewMatrix)) * viewDir);
    vec3 worldPos = CameraPos + worldDir * dist;

    // ---------------------------------------------------------
    // ② 围绕法线建一个 TBN 坐标系，把半球核摆到法线方向上
    // ---------------------------------------------------------
    vec3 up = (abs(N.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 T  = normalize(cross(up, N));
    vec3 B  = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    // 每像素随机旋转整个核（否则会看到规则的采样花纹）
    vec2  texSize = vec2(textureSize(gDepth, 0));
    float angle   = Hash21(floor(vUV * texSize / 4.0)) * 6.2831853;
    float ca = cos(angle), sa = sin(angle);
    mat3 rot = mat3(vec3(ca, -sa, 0.0), vec3(sa, ca, 0.0), vec3(0.0, 0.0, 1.0));

    // ---------------------------------------------------------
    // ③ 逐个采样点判定 —— 注意这里有两层"合理性"判断，缺一个都会出怪东西
    // ---------------------------------------------------------
    const float bias = max(BIAS, dist * BIAS_REL);

    float occlusion = 0.0;
    for (int i = 0; i < KERNEL_SIZE; ++i) {
        vec3 sampleWorld = worldPos + (TBN * (rot * HemisphereSample(i))) * RADIUS;

        // 投影回屏幕（世界 → 裁剪 → NDC → UV）
        vec4 clip = Projection * (ViewMatrix * vec4(sampleWorld, 1.0));
        if (clip.w <= 0.0) continue;                 // 落在相机后面
        vec3 ndcS = clip.xyz / clip.w;
        if (abs(ndcS.x) > 1.0 || abs(ndcS.y) > 1.0) continue;   // 屏幕外，没有深度可查

        vec2  suv        = ndcS.xy * 0.5 + 0.5;
        float sceneDist  = texture(gDepth, suv).r;   // 那个像素上几何的"到相机距离"
        float sampleDist = distance(sampleWorld, CameraPos);

        float diff = sampleDist - sceneDist;         // > bias 表示采样点藏在那个几何后面
        if (diff <= bias) continue;                  // 这个采样点没被挡 → 不用继续算了

        // ★★ 距离检查（range check）—— "猴头周围那一圈"的解药
        //   上面那个判断只说明"几何挡在采样点前面"，但没区分它是"近处的凹角"
        //   还是"好几单位之外的另一块几何"。按"遮挡物离采样点有多远"加权：
        //     diff ≈ 半径以内（真凹角）→ 权重 1；diff 变成好几个半径 → 权重趋近 0
        float rangeCheck = smoothstep(0.0, 1.0, RADIUS / diff);

        // ★★ normal check —— 遮挡物必须"朝着"接收者，才可能真的挡住光
        //
        //   什么时候会不成立：采样点"穿过了几何"，落到某个背对着接收者的面上
        //   （薄物体、耳朵这种尖锐薄片、以及轮廓附近）。那种几何在空间上确实
        //   在采样点前面，但它并不是一个真正的凹角 —— 不算遮挡。
        //
        //   ★ 阈值为什么取【负数】而不是 0：
        //     真实的墙角/凹角，两个面是互相垂直的（dot ≈ 0）。
        //     阈值取 0 或正数 → 把墙角本身的 AO 也一起杀掉了（那就白做了）。
        //     所以只排除"明显背对着"的面（dot < 0）。
        vec3 rawN = texture(gNormal, suv).xyz;
        float normalCheck = 1.0;
        if (dot(rawN, rawN) > 0.01) {                       // 先确认那个像素有几何（背景的法线是 0）
            vec3 sampleN = normalize(rawN);                 // 手动归一化，避免 normalize(0,0,0) 出 NaN
            normalCheck = smoothstep(kNormalMin, 0.0, dot(sampleN, N));
        }

        occlusion += rangeCheck * normalCheck;
    }

    float ao = 1.0 - occlusion / float(KERNEL_SIZE);

    // ---------------------------------------------------------
    // ④ 随距离淡出 —— 远处本来就不该由屏幕空间来决定明暗
    // ---------------------------------------------------------
    float distFade = 1.0 - smoothstep(FADE_START, FADE_END, dist);
    ao = mix(1.0, ao, distFade);

    fragAO = clamp(ao, 0.0, 1.0);
}
