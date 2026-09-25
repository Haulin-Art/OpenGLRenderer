#include "OpenGLMesh.h"

// ---- 构造函数: 所有 GPU 句柄初始化为 0（无效状态） ----
OpenGLMesh::OpenGLMesh()
    : VAO(0), VBO(0), EBO(0), mIndexCount(0)
{
}

// ---- 析构函数: 释放 GPU 端资源 ----
// 三个对象可以一次性删除:
//   glDeleteVertexArrays(1, &VAO);
//   glDeleteBuffers(1, &VBO);
//   glDeleteBuffers(1, &EBO);
// 参数 1 表示"删除 1 个对象"
OpenGLMesh::~OpenGLMesh()
{
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
}

// ---- 上传数据到 GPU ----
// 函数重载，可以选择直接输入数据，或者输入ObjMeshData
void OpenGLMesh::SetData(const float* vertices, int vertexCount,
                   const unsigned int* indices, int indexCount)
{
    setDataInternal(vertices, vertexCount, indices, indexCount);
}
void OpenGLMesh::SetData(const ObjMeshData& objMeshData)
{
    setDataInternal(objMeshData.vertices.data(),                   // 顶点数组
                 static_cast<int>(objMeshData.vertices.size()), // 顶点数
                 objMeshData.indices.data(),                    // 索引数组
                 static_cast<int>(objMeshData.indices.size())); // 索引数
}


// ============================================
// SetData — 上传顶点和索引数据到 GPU 显存
//
// 这是 GPU 渲染的前置步骤。数据上传后存储在显存中，
// 后续每次 draw() 都直接使用显存里的数据，不再经过 CPU。
//
// GL_STATIC_DRAW: 告诉 GPU 驱动"这些数据不会频繁修改"，
// 驱动可以把它们放在读取速度最快的显存区域。
// ============================================
void OpenGLMesh::setDataInternal(const float* vertices, int vertexCount,
                   const unsigned int* indices, int indexCount)
{
    mIndexCount = indexCount;  // 保存索引数量，供 draw() 使用

    // ==========================================
    // 步骤 1: 创建 GPU 对象
    // ==========================================
    // glGenXxx: 在 GPU 上分配对象，返回 ID（句柄）
    // 这相当于在 GPU 上"开了一块空地"，还没放数据
    glGenVertexArrays(1, &VAO);   // 生成 1 个 VAO，ID 存入 &VAO
    glGenBuffers(1, &VBO);        // 生成 1 个缓冲对象
    glGenBuffers(1, &EBO);

    // ==========================================
    // 步骤 2: 绑定 VAO（后续所有设置都记录在 VAO 里）
    // ==========================================
    // VAO 是一个"配置容器"。绑定 VAO 后:
    //   - 后续绑定 VBO/EBO → 记录在 VAO
    //   - 后续 glVertexAttribPointer → 记录在 VAO
    //   - 后续 glEnableVertexAttribArray → 记录在 VAO
    // 之后只需要 bind 这个 VAO，之前所有设置一键恢复
    glBindVertexArray(VAO);

    // ==========================================
    // 步骤 3: 上传顶点数据到 VBO
    // ==========================================
    // GL_ARRAY_BUFFER: 顶点属性缓冲类型
    //   其他类型: GL_ELEMENT_ARRAY_BUFFER (索引), GL_UNIFORM_BUFFER (uniform 块)
    glBindBuffer(GL_ARRAY_BUFFER, VBO);

    // glBufferData: 把 CPU 内存数据复制到 GPU 显存
    // 参数:
    //   GL_ARRAY_BUFFER    — 目标缓冲类型
    //   vertexCount * sizeof(float) — 数据总字节数
    //   vertices           — CPU 端数据地址
    //   GL_STATIC_DRAW     — 用法提示: 数据上传一次，绘制多次
    //                         (其他选择: GL_DYNAMIC_DRAW 频繁修改, GL_STREAM_DRAW 每帧修改)
    glBufferData(GL_ARRAY_BUFFER, vertexCount * sizeof(float), vertices, GL_STATIC_DRAW);

    // ==========================================
    // 步骤 4: 上传索引数据到 EBO
    // ==========================================
    // GL_ELEMENT_ARRAY_BUFFER: 索引缓冲类型
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexCount * sizeof(unsigned int),
                 indices, GL_STATIC_DRAW);

    // ==========================================
    // 步骤 5: 描述顶点数据格式（顶点属性指针）
    // ==========================================
    // 我们的顶点格式 (每个顶点 6 个 float):
    //   ┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
    //   │  pos.x   │  pos.y   │  pos.z   │ color.r  │ color.g  │ color.b  │
    //   │ float(4) │ float(4) │ float(4) │ float(4) │ float(4) │ float(4) │
    //   └──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘
    //   |←—————— 位置 (12 字节) ——————→|←—————— 颜色 (12 字节) ——————→|
    //   |←—————————— 共 24 字节 ——————————————————————————→|

    const int stride = 8 * sizeof(float);  // 一个顶点的总字节数 = 32，顶点位置+顶点法线+顶点uv

    // ---- 属性 0: 顶点位置 (location = 0，对应着色器里的 layout(location=0)) ----
    // glVertexAttribPointer 参数:
    //   0          — 属性编号 (对应着色器的 layout location)
    //   3          — 这个属性由 3 个值组成 (x, y, z)
    //   GL_FLOAT   — 每个值的类型是 float
    //   GL_FALSE   — 不需要归一化 (如果是整数颜色，GL_TRUE 会把 0-255 映射到 0-1)
    //   stride     — 步长: 从一个顶点开头到下一个顶点开头的字节数
    //   (void*)0   — 偏移: 这个属性从顶点开头的第几个字节开始读
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);  // 启用属性 0

    // ---- 属性 1: 顶点法线 (location = 1) ----
    // 偏移 = 3 * sizeof(float) = 12 字节（跳过前面的 x, y, z）
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);  // 启用属性 1

    // ---- 属性 2：顶点UV (location = 2) ----
    // 偏移 = 6 * sizeof(float) = 24 字节（跳过前面的 x, y, z, nx, ny, nz）
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);  // 启用属性 2

    // ==========================================
    // 步骤 6: 解绑（清理绑定状态，防止意外修改）
    // ==========================================
    // 先解绑 VBO，再解绑 VAO。
    // 解绑 VAO 后，之前的 VBO/EBO 绑定 + 属性设置仍然保存在 VAO 里。
    // GL_ARRAY_BUFFER 绑回 0 防止后续代码误操作 VBO 数据。
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    // EBO 不能先于 VAO 解绑！VAO 解绑前 EBO 必须保持绑定。
    // 解绑顺序: VBO → VAO → EBO (但 VAO 解绑后 EBO 自动解绑)
}

// ============================================
// draw — 绘制网格
//
// 一次 draw 调用 = 把所有三角形批量提交给 GPU
// GPU 会: 顶点着色器(24顶点) → 光栅化(12三角形) → 片段着色器(所有覆盖的像素)
// 这一切都在显卡硬件上并行完成
// ============================================
void OpenGLMesh::Draw() const
{
    // 绑定 VAO — 恢复所有顶点属性设置
    glBindVertexArray(VAO);

    // glDrawElements: 用索引绘制
    // 参数:
    //   GL_TRIANGLES       — 每 3 个索引 = 1 个三角形
    //   mIndexCount        — 索引数量 (如 36 个索引 = 12 个三角形)
    //   GL_UNSIGNED_INT    — 索引数据类型 (每个索引是 unsigned int)
    //   0                  — 从 EBO 的第 0 个字节开始读
    glDrawElements(GL_TRIANGLES, mIndexCount, GL_UNSIGNED_INT, 0);

    // 解绑 VAO（解绑不是必须的，但能防止后续代码意外影响状态）
    glBindVertexArray(0);
}