#pragma once

#include <string>
#include <vector>

// ============================================
// ObjLoader — 读取 .obj 模型文件（基于 tinyobjloader）
//
// 为什么不能直接把 OBJ 的数据丢给 GPU?
//   OBJ 里 位置(v) / UV(vt) / 法线(vn) 是**三套互相独立的索引**，
//   f 行长这样:  f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3
//   而 GPU 只认**单一索引**（一个索引同时决定位置/法线/UV）。
//
// 所以要做一个"顶点展开(去重)":
//   把每个 (v, vt, vn) 组合当成一个新顶点，相同的组合只生成一次。
//   位置相同但 UV/法线接缝不同的角会被拆成多个顶点。
//
//   这就是立方体从文件里的 8 个位置点，变成 GPU 需要的 24 个顶点的原因。
// ============================================

// 加载结果 —— 已经是 GPU 可以直接使用的形状
struct ObjMeshData
{
    // 交错顶点数据，每个顶点固定 8 个 float:
    //   位置 x, y, z  |  法线 nx, ny, nz  |  UV u, v
    // 文件里没有法线或 UV 时，对应分量填 0。
    std::vector<float> vertices;

    // 三角形索引，每 3 个组成一个三角形
    std::vector<unsigned int> indices;

    int VertexCount() const { return static_cast<int>(vertices.size() / 8); }
    int IndexCount() const { return static_cast<int>(indices.size()); }
};

// 读取 .obj 文件并做顶点展开。成功返回 true。
// 会把统计信息（文件里的位置/法线/UV 数、展开后的顶点数等）打印到控制台。
bool LoadObj(const std::string& path, ObjMeshData& out);

// 把加载结果打印到控制台：
// 顶点数 / 索引数 / 三角形数，以及前几个顶点和前几个索引。
// 用来肉眼检查数据是否正确。
void PrintObjData(const ObjMeshData& data);
