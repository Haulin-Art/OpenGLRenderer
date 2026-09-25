#pragma once
// 用于创建渲染器实例的工厂函数，唯一知道所有后端的实现类
#include "config.h"
#include "Renderer.h"

#if defined(OPENGL_RENDERER)
    #include "OpenGLRenderer.h"
#elif defined(VULKAN_RENDERER)
    #include "VulkanRenderer.h"
#endif