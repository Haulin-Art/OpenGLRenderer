#version 460 core

in vec3 vertexNormal; // 从顶点着色器传入的法线变量
in vec3 vertexPos;
in vec3 posWS;

uniform vec3 mainLightPos; // 主光源位置
uniform vec3 mainLightColor; // 主光源颜色
uniform vec3 CameraPos; // 相机位置

out vec4 fragColor; // 输出到帧缓冲的颜色变量


// ============================================================
// 核心：用 fwidth 做屏幕空间抗锯齿的网格
// p: 世界坐标 xz
// spacing: 网格间距 (x方向, z方向)
// lineWidthPx: 线宽（像素）(x方向, z方向)
// ============================================================
float gridMask(vec2 p, vec2 spacing, vec2 lineWidthPx,vec2 mul) {
    vec2 coord = p / spacing;
    // 到最近网格线的距离（三角波，0=线上，0.5=格子中心）
    vec2 d = abs(fract(coord - 0.5) - 0.5);
    // 当前像素覆盖的 coord 变化率（关键！）
    vec2 fw = fwidth(coord);
    // 抗锯齿：线宽转换为 coord 空间，smoothstep 做柔和边缘
    vec2 line = 1.0 - smoothstep(vec2(0.0), fw * lineWidthPx, d);
    line *= mul;
    return clamp(line.x + line.y, 0.0, 1.0);
}

void main()
{
    float lambert = dot(vertexNormal, normalize(mainLightPos));
    lambert = lambert * 0.5 + 0.5;
    vec3 diffuse = mainLightColor * lambert;


    vec2 coord = vertexPos.xz *100.0;
    // 到最近网格线的距离（三角波，0=线上，0.5=格子中心）
    vec2 d = abs(fract(coord - 0.5) - 0.5);
    vec2 fw = fwidth( coord);
    // 抗锯齿：线宽转换为 coord 空间，smoothstep 做柔和边缘
    vec2 line = 1.0 - smoothstep(vec2(0.0), fw * 1.0, d);

    // ---------- 多层网格（全部用 fwidth 抗锯齿） ----------
    // 小网格：间距 0.01，线宽 1.0 像素
    float net = gridMask(vertexPos.xz, vec2(0.01), vec2(1.0),vec2(1.0, 1.0));
    float alpha = net;
    float fade = smoothstep(70.0,5.0,distance(CameraPos,posWS));
    fade *= fade;
    net *= fade;
    
    // 大网格：间距 0.1，线宽 1.5 像素
    float bigNet = gridMask(vertexPos.xz, vec2(0.1), vec2(1.0),vec2(1.0, 1.0));
    bigNet *= fade;
    
    // 红轴（沿 z 方向的线，x 为整数）：间距 1.0，线宽 2.0 像素
    float redNet = gridMask(vertexPos.xz, vec2(1.0, 10000.0), vec2(1.0, 0.0),vec2(1.0, 0.0));
    float fade2 = smoothstep(100.0,40.0,distance(CameraPos,posWS));
    fade2 *= fade2;
    redNet *= fade2;
    
    // 蓝轴（沿 x 方向的线，z 为整数）：间距 1.0，线宽 2.0 像素
    float blueNet = gridMask(vertexPos.xz, vec2(10000.0, 1.0), vec2(0.0, 1.0),vec2(0.0, 1.0));
    blueNet *= fade2;

    vec3 netColor = mix(vec3(0.25),vec3(0.35), net);
    netColor = mix(netColor, vec3(0.5), bigNet);
    netColor = mix(netColor, vec3(1.0,0.0,0.0), redNet);
    netColor = mix(netColor, vec3(0.0,0.0,1.0), blueNet);

    fragColor = vec4(netColor, alpha); // 将顶点颜色传递给帧缓冲
    //fragColor = vec4(vec3(smoothstep(70.0,40.0,distance(CameraPos,posWS))), 1.0); // 将顶点颜色传递给帧缓冲
}