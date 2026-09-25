#version 460 core

in vec3 vertexNormal; // 从顶点着色器传入的法线
in vec3 posWS;        // 世界坐标（阴影已经搬到屏幕空间算了，这里暂时用不到；留着以后雾效之类用）

uniform vec3 mainLightPos;   // 主光源位置
uniform vec3 mainLightColor; // 主光源颜色
uniform vec3 baseColor;

// ============================================================
// ★ 材质着色器现在只管"材质"该管的事。
//
//   以前这里塞着完整的 PCSS（~70 行的 blocker search + 可变半径 PCF），
//   意味着：每个材质像素都要重跑一遍 32 次采样；同一片屏幕被多个物体覆盖时还要重复算；
//   也没法整体降分辨率、没法单独给阴影做去噪。
//
//   现在阴影由 ScreenShadowPass 在【屏幕空间】算好并模糊过，
//   材质这边只做一次采样 —— 就下面这两行。
//
//   （顺带：所有屏幕空间的结果都用同一个套路 ——
//     gl_FragCoord 是当前像素的窗口坐标，除以【屏幕尺寸】就是 [0,1] 的 UV。
//     注意是屏幕尺寸，不是那张纹理的尺寸 —— 见下面 main 里的说明。）
// ============================================================
uniform sampler2D screenShadow;   // 屏幕空间阴影：1 = 被照亮，0 = 完全在阴影里
uniform sampler2D aoMap;          // 屏幕空间 AO：1 = 没被遮挡
uniform sampler2D ssgiMap;        // ★ 屏幕空间间接光（SSGI，半分辨率）：【加到环境光】上的一项
uniform vec2      screenSize;     // ★ 屏幕尺寸（像素）。屏幕空间纹理的 UV 必须用它当分母

out vec4 fragColor;

void main()
{
    float lambert = max(0.0, dot(vertexNormal, normalize(mainLightPos)));
    vec3  diffuse = mainLightColor * lambert ;

    // ★ 屏幕空间的 UV = 当前像素的窗口坐标 / 屏幕尺寸。
    //   千万不要写成 gl_FragCoord.xy / textureSize(那张纹理) ——
    //   只有在"纹理和屏幕同分辨率"时两者才相等。阴影纹理是半分辨率的，
    //   用 textureSize 会让 UV 变成 0~2，画面就被缩小、贴到左下角。
    vec2 screenUV = gl_FragCoord.xy / screenSize;

    float shadow   = texture(screenShadow, screenUV).r;
    float ao       = texture(aoMap,        screenUV).r;
    vec3  indirect = texture(ssgiMap,      screenUV).rgb;

    // 物理上 AO 只该削弱环境光（这里的 0.2 那一项），shadow 削弱直接光。
    //
    // ★ SSGI 的作用就是【把那个写死的 0.2 换成真的算出来的环境光】：
    //     环境光 = 常数兜底(0.2) + 屏幕空间间接光
    //   然后两者一起被 AO 削弱 —— 分工是：AO 管"这里该不该暗"，SSGI 管"光从哪儿来"。
    //
    //   ★ 关掉 SSGI 时 SSGIPass 会把纹理清成 0 → indirect = 0
    //     → 这一行退化成原来的 `0.2 * ao`，和加 SSGI 之前【逐像素一致】。
    vec3 ambient = vec3(0.2) + indirect*1.0;

    vec3 cc = mix(vec3(1.0,0.0,0.0),vec3(0.0,1.0,0.0),step(0.0,(fract(posWS.x*0.25)-0.5)*(fract(posWS.z*0.25)-0.5)));
    cc = baseColor == vec3(1.0,0.0,0.0) ? cc : vec3(1.0);

    fragColor = vec4((diffuse * shadow * 0.8 + ambient * ao)* cc, 1.0);
}
