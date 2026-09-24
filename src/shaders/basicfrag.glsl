#version 460 core

in vec3 vertexNormal; // 从顶点着色器传入的法线变量

uniform vec3 mainLightPos; // 主光源位置
uniform vec3 mainLightColor; // 主光源颜色

out vec4 fragColor; // 输出到帧缓冲的颜色变量

void main()
{
    float lambert = dot(vertexNormal, normalize(mainLightPos));
    lambert = lambert * 0.5 + 0.5;
    vec3 diffuse = mainLightColor * lambert;

    fragColor = vec4(diffuse, 1.0); // 将顶点颜色传递给帧缓冲
}