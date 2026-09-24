#pragma once

#include <glm/glm.hpp>

// ============================================
// Camera — 轨道摄像机（围绕原点旋转）
//
// 思路: 不直接存摄像机位置，而是用"球坐标"三个数描述它在以原点
//       为中心的球面上的位置:
//         camYaw     水平角（绕 Y 轴）
//         camPitch   俯仰角（上下）
//         camRadius  到原点的距离
//       Update() 每帧把这 3 个数换算成 Position / ViewMatrix。
// ============================================
class Camera
{
    public:
        Camera();
        ~Camera();

        void SetViewportSize(const glm::vec2& size);

        // 鼠标拖拽用
        bool   dragging = false;   // 左键现在是不是按着

        // 鼠标位置（由 main 的回调写入）
        double mouseX = 0.0;
        double mouseY = 0.0;

        // 摄像机的状态（三个数描述它在球面上的位置）
        float camYaw    = 45.0f;   // 水平角，单位: 度
        float camPitch  = 20.0f;   // 俯仰角，单位: 度
        float camRadius = 8.0f;    // 离原点多远
        float sensitivity = 0.25f; // 每移动 1 像素，角度变化多少度

        // 左键按下时调用。
        // 除了置位 dragging，还会把 lastX/lastY 对齐到当前光标位置，
        // 否则第一次拖动时 (mouseX - lastX) 是个巨大的值，画面会猛跳一下。
        void BeginDrag(double x, double y);

        // 左键松开时调用
        void EndDrag();

        // 更新摄像机状态，每帧调用一次
        void Update();

        // 视图矩阵
        glm::mat4 ViewMatrix = glm::mat4(1.0f);
        glm::vec3 Position = glm::vec3(0.0f, 0.0f, 5.0f);

        // 投影矩阵
        glm::mat4 ProjectionMatrix = glm::mat4(1.0f);

    private:
        // 视图大小
        glm::vec2 viewportSize = glm::vec2(800, 600);
        float aspectRatio = 800.0f / 600.0f;

        double lastX = 0.0;        // 上一次鼠标的 x
        double lastY = 0.0;        // 上一次鼠标的 y
};
