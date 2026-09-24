#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>   // glm::value_ptr，传给 glUniformMatrix4fv 用

#include "Mesh.h"
#include "Shader.h"

// 物体的变换
struct Transform {
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
};
// 最小渲染命令块
struct RenderCommand {
    Mesh* mesh;
    Shader* shader;
    Transform transform;
};