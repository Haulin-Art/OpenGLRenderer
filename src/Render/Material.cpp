#include "Material.h"

Material::Material(IShader* shader) : m_Shader(shader) {
}
Material::~Material() {
}
void Material::SetShader(IShader* shader) {
    m_Shader = shader;
}