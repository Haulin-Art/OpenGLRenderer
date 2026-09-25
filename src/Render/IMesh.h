// 抽象的Mesh接口
#pragma once
#include "ObjLoader.h"

class IMesh {
    public:
        virtual ~IMesh() = default;
        // 直接根据数据设置
        virtual void SetData(const float* vertices, int vertexCount,
                       const unsigned int* indices, int indexCount) = 0;
        // 根据ObjMeshData设置
        virtual void SetData(const ObjMeshData& objMeshData) = 0;
        // 绘制
        virtual void Draw() const = 0;
};