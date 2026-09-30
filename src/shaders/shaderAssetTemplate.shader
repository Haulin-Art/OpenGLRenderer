// --- MyStandard.shader ---
// 全局属性（Material 面板参数）
Properties {
    uniform vec3 baseColor;
}

// 公共代码块（会被预处理器注入到每个 Stage 前面）
Common {

}

// Base Pass 的实现
Pass "Base" {

    Vertex {
        // 通用库文件，内部包含 MVP 矩阵，主光源位置和颜色
        #include "/src/shaders/shaderLibrary/Common.glsl"

        layout (location = 0) in vec3 aPos; // 顶点位置属性
        layout (location = 1) in vec3 aNormal; // 顶点法线
        layout (location = 2) in vec2 aTexCoor; // 顶点UV

        out vec3 vertexNormal; // 输出到片段着色器的法线变量
        out vec3 posWS;
        out vec3 normalWS;

        void main()
        {
            gl_Position = ProjectionMatrix * ViewMatrix * ModelMatrix * vec4(aPos, 1.0); // 将顶点位置传递给裁剪空间
            vertexNormal = aNormal; // 将顶点法线传递给片段着色器
            posWS = (ModelMatrix * vec4(aPos, 1.0)).xyz;

            mat3 normalMat3 = transpose(inverse(mat3(ModelMatrix)));
            normalWS = normalMat3*aNormal;
        }
    }
    
    Fragment {
        // 通用库文件，内部包含 MVP 矩阵，主光源位置和颜色
        #include "/src/shaders/shaderLibrary/Common.glsl"
        // Pass中间文件，包含平面空间阴影、AO、SSGI纹理
        #include "/src/shaders/shaderLibrary/PassScreenParams.glsl"
        // PBR
        #include "/src/shaders/shaderLibrary/PBRCookTorrance.glsl"

        in vec3 vertexNormal; // 从顶点着色器传入的法线
        in vec3 posWS;        // 世界坐标（阴影已经搬到屏幕空间算了，这里暂时用不到；留着以后雾效之类用）
        in vec3 normalWS;

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

            PBR_Light light;
            light.direction = normalize(mainLightPos);
            light.color = mainLightColor;
            light.intensity = 3.0;

            PBR_Surface surface;
            surface.albedo = cc ;
            surface.roughness = 1.0;
            surface.metallic = 0.0;

            PBR_Ambient env = PBR_MakeAmbient(0.3);
            PBR_Visibility vis = PBR_MakeVisibility(shadow, ao);   // ← shadow 也在里面了

            vec3 pbr_light = PBR_ShadeDirectional(posWS, normalWS,CameraPos,light,surface,env,1.0,vis);


            vec3 final_color = pbr_light + indirect;

            //fragColor = vec4((diffuse * shadow * 0.8 + ambient * ao)* cc, 1.0);
            fragColor = vec4(final_color, 1.0);
        }
    }
}