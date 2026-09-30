#version 450

// ============================================================
// Vulkan 三角形的顶点着色器
//
// ★ 和 OpenGL 那边最不一样的一点：这里【完全没有顶点缓冲】。
//   三个顶点的位置/颜色直接写在着色器里，靠 gl_VertexIndex 索引出来。
//   Vulkan 允许顶点着色器不带任何 vertex input binding —— 只要 pipeline 里
//   把 vertexBindingDescriptionCount / vertexAttributeDescriptionCount 都设成 0。
//   代价是三角形是"焊死"在裁剪空间里的（不参与任何矩阵变换），
//   好处是这一版不需要 vertex buffer / index buffer / staging buffer / 内存类型选择
//   —— 也就是"最低依赖"能做到的最短路径。
//
// ★ 坐标系差异（第一次写 Vulkan 最容易懵的地方）：
//   Vulkan 的裁剪空间是 y 轴【向下】的（和 D3D 一样），OpenGL 是向上。
//   所以"三角形的尖朝上"在 Vulkan 里是【负 y】。
//   另外 z ∈ [0,1]（OpenGL 是 [-1,1]），这里 z=0 表示在近平面附近。
//
// ★ 另一个必须知道的：Vulkan 的三角形【绕序】决定正反面。
//   本文件顶点顺序：顶点0(上) → 顶点1(右下) → 顶点2(左下)
//   在 y 向下的坐标系里这算【顺时针(counter-clockwise 的反面)】，
//   所以 pipeline 里必须用 VK_FRONT_FACE_CLOCKWISE（或干脆关掉剔面）。
// ============================================================

layout (location = 0) out vec3 fragColor;

vec2 positions[3] = vec2[](
    vec2( 0.0, -0.5),     // 上（Vulkan 里 y 向下，所以 -0.5 是上方）
    vec2( 0.5,  0.5),     // 右下
    vec2(-0.5,  0.5)      // 左下
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),  // 红
    vec3(0.0, 1.0, 0.0),  // 绿
    vec3(0.0, 0.0, 1.0)   // 蓝
);

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor   = colors[gl_VertexIndex];
}
