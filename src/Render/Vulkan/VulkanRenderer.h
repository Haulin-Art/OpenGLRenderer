#pragma once
#include "Renderer.h"

#include <vector>
#include <string>

// ★ 先 include Vulkan 头文件，再定义 GLFW_INCLUDE_VULKAN 去 include GLFW：
//   只有这样 GLFW 才会声明 glfwCreateWindowSurface / glfwGetRequiredInstanceExtensions，
//   否则编译期会出现"隐式声明"或者找不到符号。
//   （GLFW 头文件本身不会拖进 OpenGL —— 除非你显式定义 GLFW_INCLUDE_GLCOREARB 之类的宏。）
#include <vulkan/vulkan.h>
#ifndef GLFW_INCLUDE_VULKAN
    #define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

// ============================================================
// VulkanRenderer —— Vulkan 后端（当前目标：画出一个三角形）
//
// ============================================================
// 【一】OpenGL 老手的心智模型对照表（★ 先看这个，后面每一步就都有位置了）
//
//   OpenGL 里的东西                      Vulkan 里对应/替代它的
//   --------------------------------     ----------------------------------------------
//   上下文 glfwMakeContextCurrent        没有"上下文"这个概念；改成 vkCreateInstance + VkDevice
//   窗口                                 还是 GLFW 窗口（但★不要给它创建 OpenGL 上下文）
//   "画到哪个窗口/屏幕"                   VkSurfaceKHR（显式对象；OpenGL 里是隐含的）
//   显卡                                  VkPhysicalDevice（显式枚举，自己挑一张）
//   随便调 gl* 就能画                     VkCommandBuffer（命令先"录"下来，再提交给 VkQueue）
//   -------------------------------      ↑ 以上是"谁在画" ↓ 以下是"画到哪"
//   默认帧缓冲 / 双缓冲                   VkSwapchainKHR（一叠"可显示"的图像；OpenGL 把它整个藏起来了）
//   glViewport / glClearColor            写在 render pass / 动态状态 / 命令里（没有全局状态）
//   glEnable / glBlendFunc / glDepthFunc 全部冻结在 VkPipeline 里（创建后不可改）
//   顶点/片段着色器源码 (.glsl)          编译好的 SPIR-V 二进制 (.spv)——Vulkan 不认源码
//   glDrawArrays / glDrawElements        vkCmdDraw（录进命令缓冲，不是立刻执行）
//   glClear(GL_COLOR_BUFFER_BIT)         render pass 的 loadOp = CLEAR（"清屏"是这一趟渲染的一部分）
//   glfwSwapBuffers / SwapBuffers        vkQueuePresentKHR
//   （没有对应的东西）                    semaphore / fence：★OpenGL 帮你做的同步，这里必须自己做
//
//   【一句话总结】
//     OpenGL：你随时戳一个全局状态机，驱动在后面帮你收拾残局。
//     Vulkan：你提前把状态写成"不可变的管线"，亲手把命令录进缓冲区，
//             再亲手管好 CPU 和 GPU 之间的同步。
//             代码多了不少，但每一步都显式、可预测 —— 这就是它性能更稳的原因。
//
// ============================================================
// 【二】初始化顺序，以及"为什么必须恰好是这个顺序"（= Init() 的调用顺序）
//
//    #   步骤                     为什么必须排在它前面（依赖链）
//   --- ---------------------    -------------------------------------------------------
//    1   CreateWindow            先有窗口；而且要先 glfwInit，才能问 GLFW"要哪些 Vulkan 扩展"
//    2   CreateInstance          第 3 步要知道 instance 才能建 surface
//    3   CreateSurface           ★ 有了它，后面才能问"哪张卡能把画面显示到这个窗口"
//    4   PickPhysicalDevice      它要拿 surface 去问设备"你支不支持显示到这里"
//    5   CreateLogicalDevice     选好卡才能建设备；后面所有 vk* 都要靠这个 mDevice
//    6   CreateSwapchain         ★ 它决定了图像的【格式和尺寸】，第 7/8/9 步都依赖它
//    7   CreateImageViews        交换链只给 VkImage（裸的显存块）；当附件用必须先包成 view
//    8   CreateRenderPass        要知道图像格式（来自交换链）才能声明附件
//    9   CreateGraphicsPipeline  要挂到 render pass 上（pipeline 和 renderPass 是配对的）
//   10   CreateFramebuffers      把 image view 和 render pass 绑成一个具体的渲染目标
//   11   CreateCommandPool       命令缓冲必须从池里分配
//   12   CreateCommandBuffers    每帧在飞一条
//   13   CreateSyncObjects       第一次 DrawFrame 之前必须就绪
//
//   清理（Cleanup）严格按【逆序】进行 —— 这也是 Vulkan 的常规纪律。
//
// ============================================================
// 【三】每帧的时间线（DrawFrame）—— 为什么一个三角形也要 4 个同步对象
//
//    CPU 线程                           GPU
//    ------------------------------     --------------------------------------
//    vkWaitForFences  ────等待────►
//    vkAcquireNextImageKHR  ──────►     [给 mImageAvailable[frame] 发信号]
//                                        （这张图像归你了，可以往里画）
//    vkResetFences
//    录制命令缓冲（纯 CPU 的活）
//    vkQueueSubmit  ──────────────►     等 mImageAvailable → 画 → 给 mRenderFinished 发信号
//                                        ↑ 从哪个阶段开始等，由 pWaitDstStageMask 指定
//    vkQueuePresentKHR  ──────────►     等 mRenderFinished → 把这张图显示出去
//    （下一帧回到第一行，继续等 fence）
//
//   ★ 为什么要 fence：CPU 比 GPU 快得多，不等的话 CPU 会冲上去把 GPU 还在用的
//     命令缓冲/图像覆盖掉。fence 就是"这一帧的 GPU 活干完了"的回执。
//   ★ kMaxFramesInFlight = 2 的含义：CPU 可以在录第 N+1 帧的同时，让 GPU 画第 N 帧。
//     设成 1 就变成完全串行（简单但慢）。
//
// ============================================================
// 【四】想真正看到这个三角形，需要改的地方 —— 只有两个文件
//
//   ① CMakeLists.txt（三处）
//        a) add_executable 清单里加：      src/Render/Vulkan/VulkanRenderer.cpp
//        b) target_include_directories 加：${CMAKE_SOURCE_DIR}/src/Render/Vulkan
//                                          ${CMAKE_SOURCE_DIR}/dependencies/vulkan/include
//        c) 链接加（导入库已经在 dependencies/vulkan/ 里了）：
//             add_library(vulkan INTERFACE)
//             target_include_directories(vulkan INTERFACE ${CMAKE_SOURCE_DIR}/dependencies/vulkan/include)
//             target_link_libraries(vulkan INTERFACE ${CMAKE_SOURCE_DIR}/dependencies/vulkan/libvulkan-1.a)
//             target_link_libraries(OpenGLRenderer PRIVATE vulkan)
//
//   ② src/config.h（一行）：把 OPENGL_RENDERER 换成 VULKAN_RENDERER
//
//   ★ 其它文件【一个都不用改】：
//       - RendererFactory.h/.cpp 里的 #elif defined(VULKAN_RENDERER) 分支早就写好了
//       - main.cpp 一行都不用动：CreateShader/CreateMesh 返回的是空实现，
//         Clear()/SwapBuffers() 是安全的空操作，三角形由 ExecuteRenderCommands 画出来
//         —— 这正是当初把 CreateShader/CreateMesh/Clear/SwapBuffers 做成接口成员的价值。
//   ★ .spv 文件已经生成在 src/shaders/ 下，运行期只读它，不需要装 glslang。
//     （只有你以后改了 .vert/.frag 才需要重新用 glslang 编一次）
//
// ★ 阶段目标（刻意做到最小）：
//     instance → surface → 物理设备 → 逻辑设备 → swapchain → image views
//     → render pass → graphics pipeline → framebuffers → command buffers
//     → 同步对象 → 每帧 acquire / record / submit / present
//
//   【不做】的东西（都是"再加一步"的独立话题）：
//     - 顶点缓冲 / 索引缓冲（三角形顶点直接写在顶点着色器里，靠 gl_VertexIndex 取）
//     - descriptor set / uniform buffer（pipeline layout 是空的）
//     - 纹理、深度附件、MSAA、push constant、多渲染通道
//     - validation layer（需要安装 Vulkan SDK 的层；本机没装，所以默认不开）
//     - 时间戳/查询、内存分配器（VMA）—— 这一版没有任何 buffer 需要分配
//
// ★ 和 OpenGLRenderer 的关系：两者【互不认识】。
//   共同点只有 IRenderer 这个接口，以及各自在 .cpp 里创建自己的 GLFW 窗口。
//
// ★ 和 IRenderer 接口的对接方式（为什么这样映射）：
//   - CreateShader() / CreateMesh() 返回【空实现】的占位对象。
//     因为上层（main.cpp）会调用 BuildFromFiles / SetData，不能返回 nullptr。
//     真正的 VulkanShader / VulkanMesh 是后面的事。
//   - Clear()：只记住清屏色。真正的"清屏"是 render pass 的 loadOp = CLEAR 干的
//     —— Vulkan 里没有"先清屏再画"这种全局操作。
//   - ExecuteRenderCommands()：忽略传进来的渲染队列，直接画三角形。
//     这样 main.cpp 的循环一行都不用改就能看到画面。
//   - SwapBuffers()：空实现。Vulkan 的"交换"是 vkQueuePresentKHR，
//     已经发生在 ExecuteRenderCommands 里（否则每帧要拆成两半、状态很难维护）。
//   - Enable/DisableRendererFeature()：空实现。Vulkan 没有"全局渲染状态"这个概念，
//     深度测试 / 混合 / 剔面全都写在 pipeline 里（创建时就固定），不存在运行时开关。
// ============================================================
class VulkanRenderer : public IRenderer {
    public:
        VulkanRenderer(int width, int height);
        ~VulkanRenderer() override;

        // 数据相关
        glm::vec2 GetWindowSize() override;

        // IRenderer 接口函数
        bool Init() override;
        void* GetWindow() override;
        void SetClearColor(const glm::vec4& color = glm::vec4(0.25f, 0.25f, 0.25f, 1.0f)) override;
        void Clear() override;

        // 封装API
        bool WindowShouldClose() override;
        void WindowTerminate() override;
        void SwapBuffers() override;
        void PollEvents() override;
        void EnableRendererFeature(BuiltInRendererFeatures feature) override;
        void DisableRendererFeature(BuiltInRendererFeatures feature) override;

        // 相关资源创建（当前是空实现，见类注释）
        IShader* CreateShader() override;
        IMesh* CreateMesh() override;

        // 执行渲染队列命令
        void ExecuteRenderCommands(const std::vector<RenderCommand>& RenderingCommandQueue,const CameraData& RenderingCameraData) override;

    private:
        // ------------------------------------------------------------
        // 初始化：严格按依赖顺序，任何一步失败就返回 false
        // ------------------------------------------------------------
        bool CreateWindow();        // glfwInit + 创建【没有 OpenGL 上下文】的窗口
        bool CreateInstance();      // VkInstance
        bool CreateSurface();       // VkSurfaceKHR（由 GLFW 创建，跨平台）
        bool PickPhysicalDevice();  // 选一张显卡
        bool CreateLogicalDevice(); // VkDevice + 取队列
        bool CreateSwapchain();     // 交换链（+ 记录 format / extent）
        bool CreateImageViews();
        bool CreateRenderPass();
        bool CreateGraphicsPipeline();
        bool CreateFramebuffers();
        bool CreateCommandPool();
        bool CreateCommandBuffers();
        bool CreateSyncObjects();

        // ------------------------------------------------------------
        // 交换链相关
        // ------------------------------------------------------------
        struct QueueFamilies {
            uint32_t graphics = 0xFFFFFFFFu;
            uint32_t present  = 0xFFFFFFFFu;
            bool Complete() const { return graphics != 0xFFFFFFFFu && present != 0xFFFFFFFFu; }
        };

        struct SwapchainSupport {
            VkSurfaceCapabilitiesKHR        caps{};
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR>   presentModes;
            bool Usable() const { return !formats.empty() && !presentModes.empty(); }
        };

        QueueFamilies    QueryQueueFamilies(VkPhysicalDevice dev) const;
        SwapchainSupport QuerySwapchainSupport(VkPhysicalDevice dev) const;

        VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
        VkPresentModeKHR   ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes) const;
        VkExtent2D         ChooseExtent(const VkSurfaceCapabilitiesKHR& caps) const;

        bool CreateSwapchainResources();    // image views + framebuffers（重建交换链时复用）
        void DestroySwapchainResources();
        bool RecreateSwapchain();           // 窗口尺寸变了 / surface 失效时重建

        // ------------------------------------------------------------
        // 每帧
        // ------------------------------------------------------------
        void DrawFrame();
        void RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);

        // ------------------------------------------------------------
        // 工具
        // ------------------------------------------------------------
        static std::vector<char> ReadFileBytes(const std::string& path);
        VkShaderModule CreateShaderModule(const std::vector<char>& code) const;

        void Cleanup();   // 幂等：可以安全地被 WindowTerminate() 和析构各调一次

        // ------------------------------------------------------------
        // 数据成员
        // ------------------------------------------------------------
        const int   WINDOW_WIDTH;
        const int   WINDOW_HEIGHT;
        GLFWwindow* mWindow = nullptr;

        // 清屏色：Clear() 只是存下来，真正用它的是 render pass 的 loadOp = CLEAR
        VkClearValue mClearValue{};

        // ---- 设备 ----
        VkInstance       mInstance       = VK_NULL_HANDLE;
        VkSurfaceKHR     mSurface        = VK_NULL_HANDLE;
        VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
        VkDevice         mDevice         = VK_NULL_HANDLE;
        VkQueue          mGraphicsQueue  = VK_NULL_HANDLE;
        VkQueue          mPresentQueue   = VK_NULL_HANDLE;
        uint32_t         mGraphicsFamily = 0xFFFFFFFFu;
        uint32_t         mPresentFamily  = 0xFFFFFFFFu;

        // ---- 交换链 ----
        VkSwapchainKHR             mSwapchain       = VK_NULL_HANDLE;
        VkFormat                   mSwapchainFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D                 mSwapchainExtent{};
        std::vector<VkImage>       mSwapchainImages;
        std::vector<VkImageView>   mSwapchainImageViews;
        std::vector<VkFramebuffer> mFramebuffers;

        // ---- 管线 ----
        VkRenderPass     mRenderPass     = VK_NULL_HANDLE;
        VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
        VkPipeline       mPipeline       = VK_NULL_HANDLE;

        // ---- 命令 ----
        VkCommandPool                mCommandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> mCommandBuffers;

        // ---- 同步：每帧一套（帧在飞 = frames in flight）----
        static const int kMaxFramesInFlight = 2;
        std::vector<VkSemaphore> mImageAvailable;   // 拿到交换链图像了
        std::vector<VkSemaphore> mRenderFinished;   // 画完了，可以显示了
        std::vector<VkFence>     mInFlightFences;   // 这一帧的 GPU 工作是否做完
        uint32_t mCurrentFrame = 0;
};
