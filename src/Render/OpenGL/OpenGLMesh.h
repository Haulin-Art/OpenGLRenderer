#pragma once
#include "IMesh.h"
#include <glad/glad.h>

class OpenGLMesh : public IMesh {
    public:

        // ---- 构造/析构 ----
        // 构造时所有句柄初始化为 0
        OpenGLMesh();

        // 析构时释放 GPU 资源 (VAO, VBO, EBO)
        // 原理同 Shader 析构函数
        ~OpenGLMesh();

        // ---- 上传数据到 GPU ----
        // vertices:  顶点数组（交错格式: x, y, z, r, g, b, x, y, z, r, g, b, ...）
        //            每个顶点占 6 个 float
        // vertexCount: vertices 数组中的 float 总数（不是顶点数！）
        // indices:   索引数组（每个 unsigned int 指向一个顶点）
        // indexCount: indices 数组中的元素个数
        //
        // 这个方法做了四件事:
        //   1. glGenVertexArrays → 创建 VAO
        //   2. glBufferData       → 把顶点和索引数据上传到 GPU 显存 (VBO + EBO)
        //   3. glVertexAttribPointer → 告诉 GPU 数据格式（位置在 offset=0，颜色在 offset=12）
        //   4. glEnableVertexAttribArray → 启用顶点属性
        void SetData(const float* vertices, int vertexCount,
                     const unsigned int* indices, int indexCount);

        // 使用ObjLoader加载obj文件
        void SetData(const ObjMeshData& objMeshData);

        // ---- 绘制 ----
        // 绑定 VAO，调用 glDrawElements 绘制三角形
        // 实际绘制的三角形数 = mIndexCount / 3
        void Draw() const;

    private:
        // VAO: 顶点数组对象 — 一个"配置包"
        //   存储: VBO 绑定 + 所有 glVertexAttribPointer 设置 + EBO 绑定
        //   之后一次 glBindVertexArray(VAO) 即可恢复所有设置
        unsigned int VAO;

        // VBO: 顶点缓冲 — GPU 显存中的顶点数据块
        //   存储: 我们上传的 vertices 数组（位置和颜色）
        unsigned int VBO;

        // EBO: 索引缓冲 — GPU 显存中的索引数据块
        //   存储: 我们上传的 indices 数组
        //   作用: 告诉 GPU "用顶点 A,B,C 画第一个三角形，用 D,E,F 画第二个..."
        unsigned int EBO;

        // mIndexCount: 索引元素总数
        //   用于 glDrawElements 的 count 参数
        //   例如 36 个索引 → 36/3 = 12 个三角形 → 6 个面 × 2 个三角形 = 一个立方体
        int mIndexCount;

        // setData的内部实现，所有Public方法都会调用此方法
        void setDataInternal(const float* vertices, int vertexCount,
                     const unsigned int* indices, int indexCount);
};