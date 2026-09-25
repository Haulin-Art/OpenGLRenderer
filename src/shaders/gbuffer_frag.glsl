#version 460 core

in vec3 vWorldPos;
in vec3 vWorldNormal;

uniform vec3 CameraPos;   // 和 basicfrag 用的是同一个 uniform（IShader::SetCamera 设置）

// ★ 材质的固有色。由 GBufferPass 在【画每个物体之前】设一次
//   （shader->SetVec3("baseColor", command.material->baseColor)）。
//   这是"per-object → per-pixel"的那一步：SSGI 是全屏 Pass，没有"当前物体"，
//   只能在屏幕空间按 UV 采这张图，所以颜色必须在这里先烘进去。
//   没有声明它的着色器收到这个名字会被静默忽略（location = -1），所以可以无脑设。
uniform vec3 baseColor;

// ★ MRT：一次绘制同时写三个颜色附件。
//   layout(location = N) 对应 glDrawBuffers 里第 N 个附件（也就是 GL_COLOR_ATTACHMENT N）。
layout (location = 0) out vec4 gNormal;   // 世界法线（xyz），w 留给以后（粗糙度等）
layout (location = 1) out float gDepth;   // 线性「世界深度」
layout (location = 2) out vec4 gAlbedo;   // 材质固有色（rgb）—— SSGI 采样命中点用它上色

void main()
{
    // ---- 附件 0：世界法线，归一化到 [-1,1] ----
    gNormal = vec4(normalize(vWorldNormal), 1.0);

    // ---- 附件 1：世界深度 ----
    // 这里存的是「点到相机的距离」（世界单位），不是硬件深度缓冲那种 [0,1] 的非线性值。
    // 好处：数值本身就是世界单位，读起来/调 SSAO 都直观，而且不需要额外的 near/far 反算。
    // 背景像素因为没有被任何几何覆盖，会保留清屏值（见 GBufferPass 里的哨兵值）。
    gDepth = distance(CameraPos, vWorldPos);

    // ---- 附件 2：albedo（固有色，还没受光）----
    // ★ 存的是"材质是什么颜色"，不是"这个像素看起来多亮"：
    //   受光、阴影、AO 都还没乘进来。SSGI 拿它当"命中点把光反射成什么颜色"，
    //   乘完再交给 BasePass，最终结果里 albedo 只被乘一次。
    gAlbedo = vec4(baseColor, 1.0);

    // 另一种常见写法是存「视图空间线性深度」-z：
    //     gDepth = -(ViewMatrix * vec4(vWorldPos, 1.0)).z;
    // 它和上面只差一个 cos(夹角)，如果你以后要"从深度精确重建视图空间坐标"，用那个更直接。
}
