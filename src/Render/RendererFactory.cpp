#include "RendererFactory.h"

// Renderer.h 内的全局函数 CreateRenderer() 的实现，返回不同后端的渲染器实例
IRenderer* CreateRenderer(int width, int height) {
    #if defined(OPENGL_RENDERER)
        return new OpenGLRenderer(width, height);
    #elif defined(VULKAN_RENDERER)
        return new VulkanRenderer(width, height);
    #else
        #error "没有选择渲染后端：请定义 OPENGL_RENDERER 或 VULKAN_RENDERER"
    #endif
}