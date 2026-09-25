#include "FullscreenQuad.h"

#include <glad/glad.h>

void FullscreenQuad::Create()
{
    if (mVAO) return;                       // 已经建过了
    // Core Profile 要求 draw 之前必须绑定一个 VAO；这个 VAO 里什么都没有也没关系，
    // 因为顶点的位置/UV 都是在顶点着色器里用 gl_VertexID 算出来的。
    glGenVertexArrays(1, &mVAO);
}

void FullscreenQuad::Destroy()
{
    if (mVAO) glDeleteVertexArrays(1, &mVAO);
    mVAO = 0;
}

FullscreenQuad::~FullscreenQuad()
{
    // ★ 必须在 GL 上下文还活着时删
    Destroy();
}

void FullscreenQuad::Draw() const
{
    glBindVertexArray(mVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);       // 只有 3 个顶点
    glBindVertexArray(0);
}
