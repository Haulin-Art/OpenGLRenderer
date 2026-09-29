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