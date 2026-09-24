#include "Camera.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>   // glm::lookAt

Camera::Camera()
{
    // 先算一次，保证第一帧就有有效的 ViewMatrix
    Update();
}

Camera::~Camera() {}

// 设置窗口大小
void Camera::SetViewportSize(const glm::vec2& size)
{
    viewportSize = size;
    aspectRatio =  (size.y > 0.0f) ? size.x / size.y : 1.0f;   // 防除零
}

// 左键按下: 开始拖拽，并把 lastX/lastY 对齐到当前光标位置
void Camera::BeginDrag(double x, double y)
{
    dragging = true;
    mouseX = x;
    mouseY = y;
    lastX = x;
    lastY = y;
}

void Camera::EndDrag()
{
    dragging = false;
}

void Camera::Update()
{
    // 只有按着左键时才根据鼠标移动调整角度
    if (dragging)
    {
        camYaw   += sensitivity * static_cast<float>(mouseX - lastX);
        camPitch -= sensitivity * static_cast<float>(mouseY - lastY);   // 屏幕 y 向下为正，所以取反

        lastX = mouseX;
        lastY = mouseY;

        // 夹紧俯仰角: 到 ±90° 时视线会和 up 共线，lookAt 会退化导致画面翻转
        if (camPitch >  89.0f) camPitch =  89.0f;
        if (camPitch < -89.0f) camPitch = -89.0f;
    }

    // ---- 矩阵计算放在 if 外面 ----
    // 不拖拽时也要算: 否则第一帧（还没拖过）ViewMatrix 还是单位矩阵，
    // 摄像机等于站在原点，画面会穿模。以后改 camRadius（滚轮缩放）也需要它立即生效。
    const float yawRad   = glm::radians(camYaw);
    const float pitchRad = glm::radians(camPitch);

    // 球坐标 → 直角坐标
    Position = glm::vec3(
        camRadius * std::sin(yawRad) * std::cos(pitchRad),
        camRadius * std::sin(pitchRad),
        camRadius * std::cos(yawRad) * std::cos(pitchRad)
    );

    // 始终看向原点
    ViewMatrix = glm::lookAt(
        Position,                        // 摄像机位置
        glm::vec3(0.0f, 0.0f, 0.0f),     // 目标位置
        glm::vec3(0.0f, 1.0f, 0.0f)      // 上方向
    );

    
    ProjectionMatrix = glm::perspective(
        glm::radians(45.0f),           // FOV: 45度
        aspectRatio,                        // aspect: 宽高比
        0.1f,                          // near: 近裁剪面
        100.0f);                       // far: 远裁剪面
}
