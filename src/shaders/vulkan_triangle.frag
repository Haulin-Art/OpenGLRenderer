#version 450

// ============================================================
// Vulkan 三角形的片段着色器
//
// ★ 和 OpenGL 的区别：
//   - 没有 gl_FragColor，必须自己用 layout(location = 0) 声明输出
//     （OpenGL 核心模式其实也是这个规则，只是很多教程还在用旧写法）
//   - location 编号必须和 pipeline 里 colorAttachment 的 blend 状态对得上
//     （这里就是唯一的那个颜色附件，location = 0）
// ============================================================

layout (location = 0) in  vec3 fragColor;
layout (location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(fragColor, 1.0);
}
