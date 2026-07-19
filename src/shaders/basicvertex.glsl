#version 460 core

layout (location = 0) in vec3 aPos; // 顶点位置属性
layout (location = 1) in vec3 aColor; // 顶点颜色属性

out vec3 vertexColor; // 输出到片段着色器的颜色变量

void main()
{
    gl_Position = vec4(aPos, 1.0); // 将顶点位置传递给裁剪空间
    vertexColor = aColor; // 将顶点颜色传递给片段着色器
}