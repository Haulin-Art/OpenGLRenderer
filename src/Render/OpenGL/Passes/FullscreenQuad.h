#pragma once

// ============================================================
// FullscreenQuad —— "全屏三角形"的 VAO 封装
//
// 所有后处理 Pass 的地基：它们不画场景里的物体，而是"对屏幕上的每个像素算点什么"。
//
// ★ 为什么是全屏三角形而不是全屏四边形：
//   3 个顶点（而不是 4 个/6 个），少一条对角线的边界，而且 uv 可以纯靠 gl_VertexID 算出来。
// ★ 为什么还需要 VAO：
//   Core Profile 规定"必须绑定一个 VAO 才能 draw"，哪怕这个 VAO 里一个属性都没有。
//   所以这里就是个空的 VAO —— 顶点数据完全由顶点着色器里的 gl_VertexID 生成，
//   不需要 VBO、不需要 attribute。
//
// 顶点着色器是共用的：src/shaders/fullscreen_vert.glsl
// ============================================================
class FullscreenQuad {
public:
    FullscreenQuad() = default;
    ~FullscreenQuad();

    // 禁止拷贝：它持有一个 GL 对象，拷贝会导致两次删除
    FullscreenQuad(const FullscreenQuad&)            = delete;
    FullscreenQuad& operator=(const FullscreenQuad&) = delete;

    void Create();          // 在 GL 上下文就绪后调一次（重复调用无副作用）
    void Destroy();

    void Draw() const;      // 绑定空 VAO + 画 3 个顶点

private:
    unsigned int mVAO = 0;
};
