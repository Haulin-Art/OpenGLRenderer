#include "RenderQueue.h"

RenderQueue::RenderQueue() {
}
RenderQueue::~RenderQueue() {
    mCommands.clear();
}
// 排序依据：不透明在前，透明在后；同 shader / 同状态 的物体连在一起画；透明物体按距离远→近
void RenderQueue::Sort(const glm::vec3& cameraPos) {
    // 这里的 auto 表示编译器自动推导类型，key 是一个 lambda 函数，返回一个元组，元组的元素类型由编译器根据返回值推导
    // 这里的 [&] 表示捕获外部变量的引用，允许在 lambda 内部访问外部变量，这里就是能使用 cameraPos
    // (const RenderCommand& c) 就是函数的参数列表，表示传入一个 const RenderCommand 类型的引用，避免拷贝 RenderCommand 对象（节省性能）
    auto key = [&](const RenderCommand& c) {
        // 获取渲染状态，这里的这个&符号表示按引用传递，避免拷贝 RenderState 对象（节省性能）
        const RenderState& rs = c.material->renderState;
        // 是否透明
        const bool  transparent = (rs.blend != BlendMode::Opaque);
        // 物体原点距离摄像机距离的平方
        const glm::vec3 d = c.transform.position - cameraPos;
        const float d2 = glm::dot(d, d);          // 比距离用平方就够，省一次开方
        // 这个元组有多个元素，排序比较的时候会按顺序比较每个元素，直到找到不同的元素为止
        return std::make_tuple(
            transparent,                                          // ① 不透明(false=0) 在前
            reinterpret_cast<uintptr_t>(c.material->GetShader()),  // ② 按 shader 分组
            rs,                                                   // ③ 按状态分组（operator<终于有人用了）
            transparent ? -d2 : d2                                // ④ 一个字段表达两种相反顺序
        );
    };
    // 什么意思，得详细讲解
    // mCommands.begin() 返回一个迭代器，指向 mCommands 的第一个元素
    // mCommands.end() 返回一个迭代器，指向 mCommands 的最后一个元素的下一个位置
    // 这两个迭代器定义了一个范围[mCommands.begin(), mCommands.end())，std::sort 会对这个范围内的元素进行排序
    std::sort(mCommands.begin(), mCommands.end(),
              // [&] 表示捕获外部变量的引用，允许在 lambda 内部访问外部变量，这里就是能使用 key
              // (const RenderCommand& a, const RenderCommand& b) 就是函数的参数列表，表示传入两个 const RenderCommand 类型的引用，避免拷贝 RenderCommand 对象（节省性能）
              // 返回值是一个 bool 类型，表示 a 是否应该排在 b 前面
              // sort 的第三个参数是一个比较函数，返回 true 表示 a 应该排在 b 前面，返回 false 表示 a 不应该排在 b 前面
              // 这里的比较函数就是用 key 函数生成的元组进行比较，元组的比较规则是按顺序比较每个元素，直到找到不同的元素为止
              [&](const RenderCommand& a, const RenderCommand& b) { return key(a) < key(b); });
}