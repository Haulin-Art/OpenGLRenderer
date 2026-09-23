#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>   // glm::value_ptr，传给 glUniformMatrix4fv 用

#include "Mesh.h"
#include "Shader.h"

// 渲染对象结构体
struct RenderObject
{
    Mesh* mesh;
    Shader* shader;
    glm::mat4 transform;
};