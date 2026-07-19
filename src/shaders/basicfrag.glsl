#version 460 core

in vec3 vertexColor; // 从顶点着色器传入的颜色变量

out vec4 fragColor; // 输出到帧缓冲的颜色变量

void main()
{
    fragColor = vec4(vertexColor, 1.0); // 将顶点颜色传递给帧缓冲
}