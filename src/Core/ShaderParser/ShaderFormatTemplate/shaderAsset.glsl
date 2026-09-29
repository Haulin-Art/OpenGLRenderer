// --- MyStandard.shader ---
// 全局属性（Material 面板参数）
Properties {
    uniform vec3 Albedo = (1.0, 1.0, 1.0);
    uniform float Roughness = 0.5;
    uniform sampler2D AlbedoTex;
}

// 公共代码块（会被预处理器注入到每个 Stage 前面）
Common {
    struct PS_INPUT {
        vec3 WorldPos;
        vec3 Normal;
    };
}
// 似乎还有有些问题？
// 我最开始是打算在我定义的这种 ShaderAsset 当中去声明使用这个的物体，会被管线当中的哪些Pass 使用的，比如 GBuffer Pass 和 Base Pass
// 然后如果使用 GBuffer Pass ，就是使用这个 ShaderAsset 当中的 Pass "GBuffer" 当中的内容
// 但是这样似乎会反复切换glProgram，而且我如果想使用Pass当中默认的 shader 呢？
// 或许该视 ShaderAsset 当中声明的 Pass 为重载的 shader，而不想切换的只声明，而没有实际内容，这可能得再规定一下，比如 Pass "GBuffer" Default ？
// 或许哪怕视为重载，比如有些使用默认的 GBuffer Shader，有些则是ShaderAsset 当中的 Pass "GBuffer" 当中的内容，对于这部分，或许在渲染时期要据此再排序一下


// GBuffer Pass 的实现
Pass "GBuffer" {
    Vertex {
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec3 aNormal;
        out PS_INPUT psInput;
        uniform mat4 u_Model;
        uniform mat4 u_ViewProj;
        void main() {
            psInput.WorldPos = (u_Model * vec4(aPos, 1.0)).xyz;
            psInput.Normal = mat3(u_Model) * aNormal;
            gl_Position = u_ViewProj * vec4(psInput.WorldPos, 1.0);
        }
    }
    
    Fragment {
        // #include 引用的实现
        #include "\src\Core\ShaderParser\ShaderFormatTemplate\PBRLibrary.glsl"
        in PS_INPUT psInput;
        layout(location = 0) out vec4 gAlbedo;
        layout(location = 1) out vec4 gNormal;
        // 引用 Properties 里的变量
        uniform vec3 Albedo; 
        void main() {
            gAlbedo = vec4(Albedo, 1.0);
            gNormal = vec4(normalize(psInput.Normal), 1.0);
        }
    }
}

// Base Pass (Forward) 的实现
Pass "Base" {
    Vertex {
        666666666;
    }
    
    Fragment {
        666666666;
    }
}

// Shadow Pass
Pass "Shadow" {
    Vertex {
        888888888;
    }
    
    Fragment {
        888888888;
    }
}