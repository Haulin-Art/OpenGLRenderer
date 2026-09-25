#version 460 core

layout (location = 0) in vec3 aPos;      // 位置
layout (location = 1) in vec3 aNormal;   // 法线
layout (location = 2) in vec2 aTexCoor;  // UV（G-Buffer 暂时不用）

uniform mat4 ModelMatrix;
uniform mat4 ViewMatrix;
uniform mat4 ProjectionMatrix;

out vec3 vWorldPos;
out vec3 vWorldNormal;

void main()
{
    vec4 worldPos = ModelMatrix * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;

    // ★ 法线必须用「模型矩阵的逆转置」变换，不能直接乘 mat3(ModelMatrix)。
    //   原因：法线是"垂直于表面"的方向，而模型矩阵（尤其在非等比缩放下）会把
    //   垂直关系破坏掉。逆转置矩阵恰好能把它变换回去。
    //   （例子：scale(1, 2, 1) 会把斜面的法线拉歪，直接用 mat3(model) 是错的。）
    //   我们的场景里 plane2 的 scale 就是 (3,1,3)，所以这一步不能省。
    mat3 normalMatrix = transpose(inverse(mat3(ModelMatrix)));
    vWorldNormal = normalize(normalMatrix * aNormal);

    gl_Position = ProjectionMatrix * ViewMatrix * worldPos;
}
