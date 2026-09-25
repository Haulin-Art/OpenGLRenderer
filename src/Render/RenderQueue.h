#pragma once
#include "RenderCommand.h"
#include <vector>
#include <cstdint>
#include <algorithm>
#include <tuple>
// 渲染队列类
// 负责存储所有渲染命令，并在每帧开始时进行排序，确保不透明物体在前，透明物体在后
class RenderQueue {
    public:
        RenderQueue();
        ~RenderQueue();
        // 提交渲染命令到队列
        void Submit(const RenderCommand& cmd) { mCommands.push_back(cmd); }
        // 清空渲染队列
        void Clear()                          { mCommands.clear(); }
        // 每帧调用：不透明在前（按 shader/状态分组），透明在后（按距离远→近）
        void Sort(const glm::vec3& cameraPos);
        const std::vector<RenderCommand>& Commands() const { return mCommands; }
    private:
        std::vector<RenderCommand> mCommands;
};