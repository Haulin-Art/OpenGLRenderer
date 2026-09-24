#include "Material.h"

Material::Material(Shader* shader) : m_Shader(shader) {
}
Material::~Material() {
}
void Material::SetShader(Shader* shader) {
    m_Shader = shader;
}