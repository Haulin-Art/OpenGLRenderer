// ============================================================================
//  PBRCookTorrance.glsl —— Cook-Torrance BRDF（直接光照 / 单盏灯）
//
//  形式：**shaderLibrary 里的可 #include 库文件**（不是资产、不是能独立编译的 shader）
//        · 没有 #version（解析器会插在源码中间，它必须在最前面）
//        · 没有 main()
//        · 没有 #include（解析器的 include 不递归，嵌套会静默失败）
//        · 不声明任何 uniform —— 全部靠参数传进来（理由见下）
//
//  在资产文件里的用法（`src/shaders/shaderAssetTemplate.shader`）：
//
//      Common {
//          struct PS_INPUT { vec3 WorldPos; vec3 Normal; };
//      }
//
//      Pass "Base" {
//          Fragment {
//              #include "/src/shaders/shaderLibrary/Common.glsl"
//              #include "/src/shaders/shaderLibrary/PassScreenParams.glsl"
//              #include "/src/shaders/shaderLibrary/PBRCookTorrance.glsl"   // ← 本文件
//
//              in PS_INPUT psInput;
//              uniform vec3  baseColor;
//              uniform float roughness;     // 或者交给 Properties { uniform float Roughness; }
//              uniform float metallic;
//              // CameraPos 由 Common.glsl 提供（★ 是 CameraPos，不是 cameraPos —— GLSL 大小写敏感）
//
//              void main() {
//                  PBR_Surface surf = PBR_MakeSurface(baseColor, roughness, metallic);
//
//                  // 用法 A：世界空间法线 + 平行光（推荐，要求顶点着色器给世界法线）
//                  vec3 direct = PBR_ShadeDirectional(
//                      psInput.WorldPos, psInput.Normal, CameraPos,
//                      PBR_MakeLight(normalize(vec3(-1.0, -2.0, -0.5)), mainLightColor, 1.0),
//                      surf);
//
//                  // 用法 B：不想动顶点着色器（法线还是对象空间）→ 用这个，自带逆转置
//                  // vec3 direct = PBR_ShadeObjectNormal(psInput.WorldPos, psInput.Normal,
//                  //                                     CameraPos, light, surf);
//
//                  // 用法 C：灯是"位置"而不是方向 → 传位置，内部算 L 和 1/r² 衰减
//                  // vec3 direct = PBR_ShadePointLight(psInput.WorldPos, psInput.Normal,
//                  //                                   CameraPos, mainLightPos, light, surf);
//
//                  // 用法 D：★ 想要"暗部不是纯黑" → 用带环境光的重载（末尾多 2 个参数）
//                  // PBR_Ambient   env = PBR_MakeAmbient(0.15);        // ← 那个"浮点"就是它
//                  // PBR_Visibility vis = PBR_MakeVisibility(shadow, ao); // shadow 管直射，ao 管环境
//                  // vec3 lit = PBR_ShadeDirectional(
//                  //     psInput.WorldPos, psInput.Normal, CameraPos, light, surf,
//                  //     env,
//                  //     1.0,      // 环境光强度（和 aoMap 的亮度同量纲）
//                  //     vis);
//                  //
//                  // ★ 库内部就是 direct*shadow + ambient*ao —— 所以外面【不要再乘】shadow/ao，
//                  //   否则暗部会被削两次，阴影边上出现台阶。
//
//                  fragColor = vec4(lit + indirect, 1.0);   // indirect（SSGI）单独加
//              }
//          }
//      }
//
//  依赖：#include 顺序上，本文件【必须在 Common.glsl 之后】——
//        只有用法 B 用到 ModelMatrix（Common.glsl 里声明）。
//
//  ★ 快捷索引：
//        暗部纯黑        → 用"带环境光"的重载 + PBR_MakeAmbient(0.15)
//        阴影边上分阶    → shadow/ao 交给 PBR_Visibility，别在外面再乘一次
//        高光太钝/太尖   → surface.roughness（不是改 D 里的常数）
//        金属不亮        → surface.metallic 还是 0
//        怀疑算错了      → PBR_DebugTerm() 把 D / G / F 单独打出来看
//
//  ---------------------------------------------------------------------------
//  ★ 为什么本文件【不声明 uniform】
//    库文件一旦声明 uniform，它就会跟着被注入到**每一个** include 它的 stage 里，
//    而"这个 uniform 到底该由谁、什么时候设"是 Pass / Material 的事，不是 BRDF 的事。
//    更实际的坑：库和 Pass 各声明一份同名 uniform 会报**重定义**；
//    而名字写错时 glGetUniformLocation 只返回 -1，**静默失效**（不报错）。
//    所以：光照和材质一律走参数。
//
//  ★ 法线空间 —— 用之前必须确认！
//    本文件的所有 N 参数都要求是【世界空间】单位向量。
//    当前 `shaderAssetTemplate.shader` 里写的是：
//        vertexNormal = aNormal;                       // ← 对象空间、未归一化
//    这是【对象空间】，和 posWS（世界空间）不是一个空间，直接传进来算出来的高光是错的。
//    两个修法（选一个）：
//      A. 顶点着色器里改成世界空间（推荐，每顶点算一次，便宜）：
//             normalMatrix = transpose(inverse(mat3(ModelMatrix)))
//             vertexNormal = normalize(normalMatrix * aNormal)
//         （`gbuffer_vert.glsl` 里就是这么做的；plane2 的 scale 是 (3,1,3)，
//           非等比缩放会破坏"垂直"关系，所以这个逆转置不能省。）
//      B. 不想动顶点着色器 → 用本文件末尾的 PBR_ShadeObjectNormal()，
//         它在片元里补那次逆转置（每像素都算，贵一些）。
//
//  ★ 光源语义 —— 和当前模板最大的一个不一致
//    当前模板：`lambert = dot(vertexNormal, normalize(mainLightPos))`
//              把「灯的位置」当「方向」用。灯离物体越远，朝向错得越厉害。
//    本文件：期望 light.direction 是【从表面指向光源】的**单位方向**。
//            所以 main.cpp 里那种"灯在 (1,2,0.4)"要么改成平行光方向，
//            要么用 PBR_ShadePointLight() 传灯的位置（它内部做 lightPos - worldPos）。
// ============================================================================


// ----------------------------------------------------------------------------
//  常数
// ----------------------------------------------------------------------------
const float PBR_PI  = 3.14159265358979323846;

// 分母保护。GLSL 里 0.0/0.0 = NaN，而 NaN 不会"变黑"，它会在后续的插值/模糊里
// 扩散成一片黑块或白块 —— 属于最难查的那类渲染 bug。所以每个可能为 0 的分母都夹一下。
const float PBR_EPS = 1e-5;


// ============================================================================
//  数据结构
// ============================================================================

// 表面材质参数
struct PBR_Surface {
    vec3  albedo;      // 固有色（线性空间！sRGB 贴图要先 pow(2.2)，否则高光会发灰）
    float roughness;   // [0,1] 0 = 镜面，1 = 完全粗糙
    float metallic;    // [0,1] 0 = 绝缘体，1 = 金属
};

// 一盏灯
struct PBR_Light {
    vec3  direction;   // ★ 单位向量，从表面【指向光源】
    vec3  color;       // 光源颜色
    float intensity;   // 强度（乘在 color 上，单独拎出来是为了方便调）
};

// 环境光 / 最低亮度 —— 用来把"背光面"从纯黑里拉出来
//
//  ★ 为什么必须有它：Cook-Torrance（以及任何 BRDF）在 NdotL <= 0 时返回**硬 0**,
//    因为"光从背后打来"在单盏灯的模型里就是没有光。但真实世界里没有绝对的暗 ——
//    总有天光、地面反弹、别的物体反射过来的光。少了这一项，背光面就是死黑一片，
//    看起来像"渲染坏了"而不是"这里很暗"。
struct PBR_Ambient {
    vec3  color;        // 环境光颜色（要偏冷的天光就填淡蓝，纯灰填 vec3(1.0)）
    float intensity;    // ★ 就是那个"浮点"：0 = 完全不加（回到纯黑），越大暗部越亮
    bool  applyAlbedo;  // true（推荐）= 环境光也乘固有色，暗部被材质染色（和直射光一致）
                        // false = 直接把 color*intensity 加上去（暗部变灰，会"发灰发雾"）
};

// 屏幕空间的可见性遮罩 —— ★ 这两个值【作用的对象完全不同】，千万别一起乘
//
//   · shadow            只削弱【直射光】。太阳被挡住 → 没有直射光。
//                       但环境光（天光 / 地面反弹）**照样存在** —— 这就是为什么
//                       背光面不应该黑，也是为什么 shadow 必须乘在 PBR 的"直射项"上、
//                       而不是最后统一乘一次：
//                           错：  (direct + ambient) * shadow     ← 暗部被两次削弱，出现台阶
//                           对：  direct * shadow + ambient       ← 台阶消失
//   · ambientOcclusion  只削弱【环境光】。AO 描述的是"这一点的半球视野被遮了多少"，
//                       它管的就是环境光那一项，和太阳挡没挡住无关。
struct PBR_Visibility {
    float shadow;             // [0,1] 1 = 被照亮（直射光全到）；0 = 完全在阴影里
    float ambientOcclusion;   // [0,1] 1 = 没被遮挡（环境光全到）
};

// ★ 用工厂函数而不是 struct 构造函数来造这几个结构体：
//   两者的字段顺序都必须和声明顺序严格一致，写反了编译器不一定报错，
//   但这种"参数顺序"的错误一旦上场就是视觉效果莫名其妙。用带名字的函数更不容易错。
PBR_Surface PBR_MakeSurface(const vec3 albedo, const float roughness, const float metallic)
{
    PBR_Surface s;
    s.albedo    = albedo;
    s.roughness = roughness;
    s.metallic  = metallic;
    return s;
}

PBR_Light PBR_MakeLight(const vec3 direction, const vec3 color, const float intensity)
{
    PBR_Light l;
    l.direction = direction;
    l.color     = color;
    l.intensity = intensity;
    return l;
}

PBR_Ambient PBR_MakeAmbient(const float intensity)
{
    PBR_Ambient a;
    a.color       = vec3(1.0);
    a.intensity   = intensity;
    a.applyAlbedo = true;
    return a;
}

PBR_Ambient PBR_MakeAmbient(const vec3 color, const float intensity, const bool applyAlbedo)
{
    PBR_Ambient a;
    a.color       = color;
    a.intensity   = intensity;
    a.applyAlbedo = applyAlbedo;
    return a;
}

// 屏幕空间阴影纹理里的值、AO 纹理里的值，直接传进来（都是 [0,1] 的浮点，1 = 不遮挡）
PBR_Visibility PBR_MakeVisibility(const float shadow, const float ambientOcclusion)
{
    PBR_Visibility v;
    v.shadow            = shadow;
    v.ambientOcclusion  = ambientOcclusion;
    return v;
}

// 只想给一个（比如还没接 AO）时用这个
PBR_Visibility PBR_MakeVisibility(const float shadow)
{
    return PBR_MakeVisibility(shadow, 1.0);
}


// ============================================================================
//  第 1 层：Cook-Torrance 的四个基本函数
// ============================================================================

// ----------------------------------------------------------------------------
//  法线分布函数 D —— GGX / Trowbridge-Reitz
//
//  回答：在这个粗糙度下，有多少比例的微观面片正好把光反射进眼睛？
//    · 粗糙度 → 0：只有 N=H 的那一个方向能反射 → D 变成尖峰（镜面高光）
//    · 粗糙度变大：能反射的面片变多，但每个方向的比例变小 → 高光变宽、变暗
//
//  NdotH = max(dot(N, H), 0)，a = roughness²（见下面说明）
// ----------------------------------------------------------------------------
float PBR_D_GGX(const float NdotH, const float a)
{
    const float a2 = a * a;
    const float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PBR_PI * d * d, PBR_EPS);
}

// ----------------------------------------------------------------------------
//  几何函数 G 的单项（Schlick-GGX）
//
//  回答：微观面片之间会【互相遮挡】——
//    ① 从光的方向看，山丘挡住了山谷（shadowing）
//    ② 从视线方向看，同理（masking）
//  所以实际反射出去的能量比理论少，粗糙表面尤其明显。
//
//  NdotX：NdotV 或 NdotL；k：见下面的取值说明
// ----------------------------------------------------------------------------
float PBR_G_SchlickGGX(const float NdotX, const float k)
{
    return NdotX / max(NdotX * (1.0 - k) + k, PBR_EPS);
}

// ----------------------------------------------------------------------------
//  几何函数 G 的完整版（Smith 方法）
//
//  ★ Smith 的核心：把 G 拆成两个独立项相乘  G = G_view · G_light
//    好处：
//      · 自动对 V 和 L 对称（物理上必须 —— 交换入射和出射方向，遮挡关系不该变）
//      · V 或 L 贴到表面（NdotX → 0）时 G → 0，正好抑制掠射角下不合理的巨大高光
//
//  ★ k 的取值有两个常见版本，别混用：
//      直接光照（本文件）：k = (α + 1)² / 8
//      IBL 环境光：        k = α² / 2
// ----------------------------------------------------------------------------
float PBR_G_Smith(const float NdotV, const float NdotL, const float k)
{
    return PBR_G_SchlickGGX(NdotV, k) * PBR_G_SchlickGGX(NdotL, k);
}

// ----------------------------------------------------------------------------
//  菲涅尔项 F（Schlick 近似）
//
//  回答：这个表面"有多像镜子"？
//    · 正对着看（cosθ = 1）→ 反射率 = F0
//    · 掠射角（cosθ → 0）→ 反射率 → 1（所以水面/桌面在很斜的角度看会变成镜子）
//
//  ★ "掠射角全都变镜子"是 Schlick 近似的精髓，也是物体边缘那圈 rim light 的来源。
// ----------------------------------------------------------------------------
vec3 PBR_F_Schlick(const float cosTheta, const vec3 F0)
{
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ----------------------------------------------------------------------------
//  F0：垂直入射时的反射率（"这个材质的基础反射色"）
//
//    · 绝缘体（metallic = 0）：F0 ≈ 0.04，且【不带颜色】（介质反射不挑波长，所以是灰的）
//    · 金属（metallic = 1）：F0 = 固有色（金属的"颜色"其实就是它反射光的颜色）
//
//  线性插值就是 metallic 这个参数的全部意义 —— 它是"有多金属"的混合权重。
// ----------------------------------------------------------------------------
vec3 PBR_F0(const vec3 albedo, const float metallic)
{
    const vec3 dielectricF0 = vec3(0.04);   // 4%：绝大多数绝缘体的值
    return mix(dielectricF0, albedo, metallic);
}


// ============================================================================
//  第 2 层：组装 Cook-Torrance BRDF
// ============================================================================

// ----------------------------------------------------------------------------
//  核心：单盏灯的直接光照
//
//  L_o = (kD · albedo/π + F·D·G / (4·NdotV·NdotL)) · lightColor · NdotL
//         └ 漫反射 ─────┘   └ 镜面 ──────────────────┘
//
//  ★ kD = (1 - F) · (1 - metallic)，一个式子同时管两件事：
//      ① 能量守恒：被镜面反射走的 (F) 不能再参与漫反射。
//         少了 (1-F)，掠射角会凭空多出能量（画面比物理亮）。
//      ② 金属没有漫反射：金属的折射光被自由电子吸收掉了，所以乘 (1 - metallic)。
//
//  N / V / L 都必须是【单位向量】。V = 表面→相机，L = 表面→光源。
// ----------------------------------------------------------------------------
vec3 PBR_CookTorrance(const vec3 N, const vec3 V, const vec3 L,
                      const vec3 radiance,
                      const PBR_Surface surface)
{
    // ---- 参数保护：粗糙度太接近 0 会让 a² → 0，高光直接消失（除零）----
    const float roughness = clamp(surface.roughness, 0.04, 1.0);
    const float metallic  = clamp(surface.metallic,  0.0,  1.0);

    const vec3  H     = normalize(V + L);
    const float NdotV = max(dot(N, V), 0.0);
    const float NdotL = max(dot(N, L), 0.0);
    const float NdotH = max(dot(N, H), 0.0);
    const float HdotV = max(dot(H, V), 0.0);

    // ---- 背面 / 掠射：这个像素收不到这盏灯的光，直接返回 0 ----
    //   ★ 必须提前 return。分母里有 4·NdotV·NdotL，让它取到 0 就是除以 0。
    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    const float a = roughness * roughness;          // α = roughness²（Disney 惯例）
    const float k = (a + 1.0) * (a + 1.0) / 8.0;    // ★ 直接光照用的 k（IBL 是 a²/2）

    const vec3  F = PBR_F_Schlick(HdotV, PBR_F0(surface.albedo, metallic));
    const float D = PBR_D_GGX(NdotH, a);
    const float G = PBR_G_Smith(NdotV, NdotL, k);

    // ---- 镜面项 ----
    //   分母里的 4 来自微表面模型的推导（雅可比项），不是凑的：少了它高光亮 4 倍。
    const vec3 specular = (D * G) * F / max(4.0 * NdotV * NdotL, PBR_EPS);

    // ---- 漫反射项 ----
    //   除以 π 是 Lambert BRDF 的定义（把"辐射率"和"辐照度"接起来）。
    //   kD 用 (1 - F)：F 是 RGB，而这里只需要一个标量权重，所以取各通道平均
    //   （工程上够用，也避免"某个通道先耗尽"的怪现象）。
    const vec3 kS = F;
    const vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    const vec3 diffuse = kD * surface.albedo / PBR_PI;

    return (diffuse + specular) * radiance * NdotL;
}


// ============================================================================
//  第 3 层：便利入口（按你手上有哪些数据选）
//
//    PBR_ShadeDirectional   平行光（太阳就是典型平行光）
//    PBR_ShadePointLight    点光源（传灯的位置，内部算 L 和 1/r² 衰减）
//    PBR_ShadeObjectNormal  法线还是对象空间时的兜底版（自带逆转置）
//
//  ★ 每个都有"带环境光"和"不带环境光"两个重载（同名不同参数个数）：
//      不带：暗部 = 0（BRDF 的本义，NdotL<=0 就是没有直射光）
//      带：  额外加一个环境光项，把背光面从纯黑里拉出来
//    参数个数不同，GLSL 的重载决议不会歧义，老代码不用改。
// ============================================================================

// ----------------------------------------------------------------------------
//  A. 平行光 / 方向光 —— 最常用（太阳就是典型平行光）
//
//  worldNormal / cameraPos / worldPos 都是世界空间；light.direction 是"表面→光源"的单位向量。
// ----------------------------------------------------------------------------
vec3 PBR_ShadeDirectional(const vec3 worldPos, const vec3 worldNormal,
                          const vec3 cameraPos,
                          const PBR_Light light,
                          const PBR_Surface surface)
{
    const vec3 N = normalize(worldNormal);
    const vec3 V = normalize(cameraPos - worldPos);
    const vec3 L = normalize(light.direction);
    return PBR_CookTorrance(N, V, L, light.color * light.intensity, surface);
}

// ----------------------------------------------------------------------------
//  B. 点光源 —— 传【灯的位置】，内部自己算 L，并按 1/r² 衰减
//
//  ★ 这个版本存在的意义：把"把灯的位置当方向用"这个经典错误从根上堵掉。
//    当前模板的 `normalize(mainLightPos)` 就属于那类错误。
//
//  attenuation = 1 / (1 + r²)：物理上是 1/r²，但纯 1/r² 在这个尺度下衰减太快
//  （r=3 时只剩 1/9），小场景里会显得很暗、很难调。这里用 1/(1+r²) 兼顾观感。
// ----------------------------------------------------------------------------
vec3 PBR_ShadePointLight(const vec3 worldPos, const vec3 worldNormal,
                         const vec3 cameraPos,
                         const vec3 lightPos, const PBR_Light light,
                         const PBR_Surface surface)
{
    const vec3  toLight = lightPos - worldPos;
    const float r2      = dot(toLight, toLight);
    const vec3  L       = toLight * inversesqrt(max(r2, PBR_EPS));
    const vec3  N       = normalize(worldNormal);
    const vec3  V       = normalize(cameraPos - worldPos);

    const float attenuation = 1.0 / (1.0 + r2);
    return PBR_CookTorrance(N, V, L, light.color * light.intensity * attenuation, surface);
}

// ----------------------------------------------------------------------------
//  C. 兜底版：顶点着色器只给了【对象空间】法线（当前 shaderAssetTemplate 就是这样）
//
//  在片元里补一次逆转置。★ 每像素都算一次 transpose(inverse(...))，比顶点版贵得多，
//  所以它是"不想动顶点着色器"时的权宜方案，不是首选。
//  需要 ModelMatrix uniform 可见（Common.glsl 已经声明了）。
// ----------------------------------------------------------------------------
vec3 PBR_ShadeObjectNormal(const vec3 worldPos, const vec3 objectNormal,
                           const vec3 cameraPos,
                           const PBR_Light light,
                           const PBR_Surface surface)
{
    const mat3 normalMatrix = transpose(inverse(mat3(ModelMatrix)));
    const vec3 worldNormal  = normalize(normalMatrix * objectNormal);
    return PBR_ShadeDirectional(worldPos, worldNormal, cameraPos, light, surface);
}


// ============================================================================
//  第 3.5 层：环境光项 + "带环境光"的重载
//
//  ★ 这一层是为了解决一个具体现象：**背光面纯黑**。
//    原因：`PBR_CookTorrance` 在 `NdotL <= 0` 时直接 `return vec3(0.0)` ——
//    单盏灯模型里"光从背后打来"就是没有光，于是背光面 = 0 = 死黑。
//    真实世界里没有绝对的暗（还有天光、地面反弹、别的物体反射的光），
//    所以要补一个**与方向无关**的环境光项。
//
//  ★ 环境光也要遵守两个 PBR 的规矩（否则会"发灰发雾"，一眼假）：
//      ① 乘 kD：环境光是被【漫反射】接收的，不该像自发光那样直接加。
//         少了它，金属会莫名其妙亮起来（金属本来就没有漫反射）。
//      ② 乘 albedo（可关）：暗部要被材质染色 —— 红球的环境光该是偏红的，
//         否则所有物体的暗部都会变成同一种灰。
// ============================================================================

// 只算环境光项（如果你要自己组合，或者在别的地方用）
//
//   visibility：只用到 ambientOcclusion —— ★ 环境光**不受 shadow 影响**。
//               太阳被挡住不等于天光没了，这正是"暗部不该纯黑"的物理依据。
//
//   ★ 参数名故意叫 ambientIrradiance 而不是 ambientColor：
//     它表达的是"从整个半球打过来的光的总量"（辐照度），不是"一盏灯的颜色"。
//     两者量纲一样，但这个命名能提醒你：它和 light.color 不是一回事。
vec3 PBR_AmbientTerm(const vec3 N, const vec3 V,
                     const PBR_Surface surface,
                     const PBR_Ambient ambient,
                     const float ambientIrradiance,
                     const PBR_Visibility visibility)
{
    const float metallic = clamp(surface.metallic, 0.0, 1.0);

    // 环境光下没有单一的 L，所以菲涅尔用"视线与法线的夹角"来算
    //   （掠射角仍然要更亮，这是 F 的物理含义，和有没有直射光无关）
    const float NdotV = max(dot(N, V), 0.0);
    const vec3  F     = PBR_F_Schlick(NdotV, PBR_F0(surface.albedo, metallic));

    // kD = (1 - F)(1 - metallic)：和直射光用同一个式子，保证两边的能量口径一致
    const vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    // 环境光也是"多个方向的光"，所以同样带 albedo/π 这个因子；
    // 但环境光没有 NdotL（已经积掉了），所以不能再乘 NdotL。
    vec3 diffuse = kD * surface.albedo / PBR_PI;

    // 不需要被材质染色时退回纯色（会偏灰，但能快速把暗部提亮）
    if (!ambient.applyAlbedo) diffuse = vec3(1.0);

    return diffuse * ambient.color * ambient.intensity * ambientIrradiance
                   * clamp(visibility.ambientOcclusion, 0.0, 1.0);
}

vec3 PBR_AmbientTerm(const vec3 N, const vec3 V,
                     const PBR_Surface surface,
                     const PBR_Ambient ambient,
                     const float ambientIrradiance)
{
    return PBR_AmbientTerm(N, V, surface, ambient, ambientIrradiance,
                           PBR_MakeVisibility(1.0, 1.0));
}

// ---- 带环境光的三个入口（和上面同名的三个形成重载）----
//
//  ★ 顺序就是这一层的全部意义：**先给直射项乘 shadow，再加环境光**。
//        direct * shadow + ambient
//    而不是
//        (direct + ambient) * shadow
//    两者的区别在阴影边上非常明显：后者会让"影子里的环境光"被 shadow 再削一次，
//    于是影子里出现一个不属于任何物理量的台阶（并且随 PCSS 的半影一起糊成一条带）。
vec3 PBR_ShadeDirectional(const vec3 worldPos, const vec3 worldNormal,
                          const vec3 cameraPos,
                          const PBR_Light light,
                          const PBR_Surface surface,
                          const PBR_Ambient ambient,
                          const float ambientIrradiance,   // 环境光强度（和 aoMap 的亮度同量纲）
                          const PBR_Visibility visibility) // shadow 只管直射，AO 只管环境
{
    const vec3 N = normalize(worldNormal);
    const vec3 V = normalize(cameraPos - worldPos);

    const vec3 direct = PBR_ShadeDirectional(worldPos, worldNormal, cameraPos, light, surface)
                      * clamp(visibility.shadow, 0.0, 1.0);
    return direct + PBR_AmbientTerm(N, V, surface, ambient, ambientIrradiance, visibility);
}

vec3 PBR_ShadePointLight(const vec3 worldPos, const vec3 worldNormal,
                         const vec3 cameraPos,
                         const vec3 lightPos, const PBR_Light light,
                         const PBR_Surface surface,
                         const PBR_Ambient ambient,
                         const float ambientIrradiance,
                         const PBR_Visibility visibility)
{
    const vec3 N = normalize(worldNormal);
    const vec3 V = normalize(cameraPos - worldPos);

    const vec3 direct = PBR_ShadePointLight(worldPos, worldNormal, cameraPos,
                                            lightPos, light, surface)
                      * clamp(visibility.shadow, 0.0, 1.0);
    return direct + PBR_AmbientTerm(N, V, surface, ambient, ambientIrradiance, visibility);
}

vec3 PBR_ShadeObjectNormal(const vec3 worldPos, const vec3 objectNormal,
                           const vec3 cameraPos,
                           const PBR_Light light,
                           const PBR_Surface surface,
                           const PBR_Ambient ambient,
                           const float ambientIrradiance,
                           const PBR_Visibility visibility)
{
    const mat3 normalMatrix = transpose(inverse(mat3(ModelMatrix)));
    const vec3 worldNormal  = normalize(normalMatrix * objectNormal);

    const vec3 direct = PBR_ShadeObjectNormal(worldPos, objectNormal, cameraPos, light, surface)
                      * clamp(visibility.shadow, 0.0, 1.0);
    return direct + PBR_AmbientTerm(worldNormal, normalize(cameraPos - worldPos),
                                   surface, ambient, ambientIrradiance, visibility);
}


// ============================================================================
//  第 4 层：一个极简的高光调试入口
//
//  把 BRDF 的某一项单独打出来看，比盯着最终画面对数字快得多：
//      fragColor = vec4(vec3(PBR_DebugTerm(worldPos, N, V, L, surface, 1)), 1.0);
//  term: 0 = 漫反射  1 = 镜面  2 = D  3 = G  4 = F(亮度)
// ============================================================================
float PBR_DebugTerm(const vec3 N, const vec3 V, const vec3 L,
                    const PBR_Surface surface, const int term)
{
    const float roughness = clamp(surface.roughness, 0.04, 1.0);
    const float metallic  = clamp(surface.metallic,  0.0,  1.0);

    const vec3  H     = normalize(V + L);
    const float NdotV = max(dot(N, V), 0.0);
    const float NdotL = max(dot(N, L), 0.0);
    const float NdotH = max(dot(N, H), 0.0);
    const float HdotV = max(dot(H, V), 0.0);
    if (NdotL <= 0.0 || NdotV <= 0.0) return 0.0;

    const float a = roughness * roughness;
    const float k = (a + 1.0) * (a + 1.0) / 8.0;

    if (term == 2) return PBR_D_GGX(NdotH, a);
    if (term == 3) return PBR_G_Smith(NdotV, NdotL, k);
    if (term == 4) return dot(PBR_F_Schlick(HdotV, PBR_F0(surface.albedo, metallic)),
                              vec3(0.3333));

    const vec3  F = PBR_F_Schlick(HdotV, PBR_F0(surface.albedo, metallic));
    const float D = PBR_D_GGX(NdotH, a);
    const float G = PBR_G_Smith(NdotV, NdotL, k);
    if (term == 0) {
        const vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        return dot(kD * surface.albedo / PBR_PI, vec3(0.3333));
    }
    // term == 1
    return dot((D * G) * F / max(4.0 * NdotV * NdotL, PBR_EPS), vec3(0.3333));
}
