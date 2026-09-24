#version 460 core

layout (location = 0) in vec3 aPos; // 顶点位置属性
layout (location = 1) in vec3 aNormal; // 顶点法线
layout (location = 2) in vec2 aTexCoor; // 顶点UV

uniform mat4 ModelMatrix;
uniform mat4 ViewMatrix;
uniform mat4 ProjectionMatrix;

out vec3 vertexNormal; // 输出到片段着色器的法线变量
out vec3 vertexPos; // 输出到片段着色器的顶点位置变量

void main()
{
    gl_Position = ProjectionMatrix * ViewMatrix * ModelMatrix * vec4(aPos, 1.0); // 将顶点位置传递给裁剪空间
    vertexNormal = aNormal; // 将顶点法线传递给片段着色器
    vertexPos = aPos; // 将顶点位置传递给片段着色器
}