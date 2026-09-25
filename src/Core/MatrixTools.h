#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>   // glm::value_ptr，传给 glUniformMatrix4fv 用

// 物体的变换
struct Transform {
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
};
// 写在头文件里的函数体，每个 include 它的 .cpp 都会生成一份强定义（nm 里显示 T），4 个 TU 就有 4 份
//  → 链接器报 multiple definition。inline 让它们变成弱符号（nm 里显示
//  W），链接器自动合并成一份。（或者把定义挪到 MatrixTools.cpp。）
inline glm::mat4 TransformToModelMatrix(const Transform& transform){
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    modelMatrix = glm::translate(modelMatrix, transform.position);
    modelMatrix = glm::rotate(modelMatrix, transform.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, transform.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, transform.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
    modelMatrix = glm::scale(modelMatrix, transform.scale);
    return modelMatrix;
}