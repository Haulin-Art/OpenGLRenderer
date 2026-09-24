#version 460 core

in vec3 vertexNormal; // 从顶点着色器传入的法线变量
in vec3 vertexPos;

uniform vec3 mainLightPos; // 主光源位置
uniform vec3 mainLightColor; // 主光源颜色

out vec4 fragColor; // 输出到帧缓冲的颜色变量

void main()
{
    float lambert = dot(vertexNormal, normalize(mainLightPos));
    lambert = lambert * 0.5 + 0.5;
    vec3 diffuse = mainLightColor * lambert;

    float net = smoothstep(0.96, 1.0, 2.0*abs(fract(vertexPos.x*100.0)-0.5)) + smoothstep(0.96, 1.0, 2.0*abs(fract(vertexPos.z*100.0)-0.5));
    net = clamp(net, 0.0, 1.0);

    float bigNet = smoothstep(0.996, 1.0, 2.0*abs(fract(vertexPos.x*10.0)-0.5)) + smoothstep(0.996, 1.0, 2.0*abs(fract(vertexPos.z*10.0)-0.5));

    float redNet = smoothstep(0.0003, 0.0, fract(abs(vertexPos.x)));
    float blueNet = smoothstep(0.0003, 0.0, fract(abs(vertexPos.z)));

    vec3 netColor = mix(vec3(0.25),vec3(0.4), net);
    netColor = mix(netColor, vec3(0.7), bigNet);
    netColor = mix(netColor, vec3(1.0,0.0,0.0), redNet);
    netColor = mix(netColor, vec3(0.0,0.0,1.0), blueNet);

    fragColor = vec4(netColor, net); // 将顶点颜色传递给帧缓冲
}