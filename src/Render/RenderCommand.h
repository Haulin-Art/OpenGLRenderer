#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>   // glm::value_ptr，传给 glUniformMatrix4fv 用

#include "MatrixTools.h"
#include "IMesh.h"
#include "Material.h"

// 最小渲染命令块
struct RenderCommand {
    IMesh* mesh;
    Material* material;
    Transform transform;
};



