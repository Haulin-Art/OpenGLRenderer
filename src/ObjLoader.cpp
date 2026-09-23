#include "ObjLoader.h"

// tinyobjloader 是单头文件库，实现部分要在一个 .cpp 里定义一次。
// 这个宏必须写在 include 之前，并且全项目只能有一个 .cpp 这样做
// （否则会重复定义，链接报错）。
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <array>
#include <iostream>
#include <map>

// 每个顶点占 8 个 float: 位置(3) + 法线(3) + UV(2)
static constexpr int kFloatsPerVertex = 8;

bool LoadObj(const std::string& path, ObjMeshData& out)
{
    // attrib 里按"三套独立数组"存放原始数据
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;

    // 最后一个参数 triangulate = true: 把四边形等多边形自动切成三角形，
    // 否则 shape.mesh.indices 里会保留多边形，用 GL_TRIANGLES 画就会错。
    const bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &err,
                                     path.c_str(), nullptr, true);

    if (!err.empty()) {
        std::cerr << "[ObjLoader] 错误: " << err << std::endl;
    }
    if (!ok) {
        std::cerr << "[ObjLoader] 加载失败: " << path << std::endl;
        return false;
    }

    std::cout << "[ObjLoader] 已加载: " << path << std::endl;
    std::cout << "  文件里的位置数: " << attrib.vertices.size() / 3
              << "  法线数: " << attrib.normals.size() / 3
              << "  UV 数: " << attrib.texcoords.size() / 2
              << "  shape 数: " << shapes.size()
              << "  材质数: " << materials.size() << std::endl;

    out.vertices.clear();
    out.indices.clear();

    // ---- 顶点展开(去重) ----
    // key = (位置索引, UV索引, 法线索引)，同一个组合只生成一个顶点
    std::map<std::array<int, 3>, unsigned int> uniqueVertices;

    for (const auto& shape : shapes) {
        for (const auto& idx : shape.mesh.indices) {
            const std::array<int, 3> key = {
                idx.vertex_index, idx.texcoord_index, idx.normal_index
            };

            // 这个 (v, vt, vn) 组合已经生成过 → 直接复用它的索引
            auto it = uniqueVertices.find(key);
            if (it != uniqueVertices.end()) {
                out.indices.push_back(it->second);
                continue;
            }

            // 新组合 → 生成一个新顶点
            const unsigned int newIndex =
                static_cast<unsigned int>(out.vertices.size() / kFloatsPerVertex);
            uniqueVertices.emplace(key, newIndex);

            // 位置（OBJ 一定有）
            out.vertices.push_back(attrib.vertices[3 * idx.vertex_index + 0]);
            out.vertices.push_back(attrib.vertices[3 * idx.vertex_index + 1]);
            out.vertices.push_back(attrib.vertices[3 * idx.vertex_index + 2]);

            // 法线（可能没有，索引为 -1）
            if (idx.normal_index >= 0) {
                out.vertices.push_back(attrib.normals[3 * idx.normal_index + 0]);
                out.vertices.push_back(attrib.normals[3 * idx.normal_index + 1]);
                out.vertices.push_back(attrib.normals[3 * idx.normal_index + 2]);
            } else {
                out.vertices.push_back(0.0f);
                out.vertices.push_back(0.0f);
                out.vertices.push_back(0.0f);
            }

            // UV（可能没有，索引为 -1）
            if (idx.texcoord_index >= 0) {
                out.vertices.push_back(attrib.texcoords[2 * idx.texcoord_index + 0]);
                out.vertices.push_back(attrib.texcoords[2 * idx.texcoord_index + 1]);
            } else {
                out.vertices.push_back(0.0f);
                out.vertices.push_back(0.0f);
            }

            out.indices.push_back(newIndex);
        }
    }

    std::cout << "  展开后 GPU 顶点数: " << out.VertexCount()
              << "  三角形数: " << out.IndexCount() / 3 << std::endl;

    return true;
}

void PrintObjData(const ObjMeshData& data)
{
    const int vertexCount = data.VertexCount();

    std::cout << "========== ObjMeshData ==========" << std::endl;
    std::cout << "顶点数: " << vertexCount
              << "  索引数: " << data.IndexCount()
              << "  三角形数: " << data.IndexCount() / 3 << std::endl;

    // 每个顶点 8 个 float: pos(3) + normal(3) + uv(2)
    const int showVertices = vertexCount < 5 ? vertexCount : 5;
    std::cout << "--- 前 " << showVertices << " 个顶点 (pos / normal / uv) ---" << std::endl;
    for (int i = 0; i < showVertices; ++i) {
        const float* v = &data.vertices[i * kFloatsPerVertex];
        std::cout << "  [" << i << "] "
                  << "pos(" << v[0] << ", " << v[1] << ", " << v[2] << ")  "
                  << "nor(" << v[3] << ", " << v[4] << ", " << v[5] << ")  "
                  << "uv(" << v[6] << ", " << v[7] << ")" << std::endl;
    }

    const int indexCount = data.IndexCount();
    const int showIndices = indexCount < 12 ? indexCount : 12;
    std::cout << "--- 前 " << showIndices << " 个索引 ---" << std::endl;
    std::cout << "  ";
    for (int i = 0; i < showIndices; ++i) {
        std::cout << data.indices[i] << " ";
    }
    std::cout << std::endl;
    std::cout << "=================================" << std::endl;
}
