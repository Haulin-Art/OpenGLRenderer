#include "VulkanRenderer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <string>

// ============================================================
// 【怎么读这个文件】—— 按 Init() 里的调用顺序读，就是 Vulkan 的学习顺序
//
//   Init()        第 1~13 步，每一步前面都有 "★" 注释解释"为什么需要它"
//   DrawFrame()   每帧的时间线：acquire → 录制 → 提交 → present
//   Cleanup()     严格逆序释放（Vulkan 的常规纪律）
//
//   只想抓"和 OpenGL 到底差在哪"，读这三处就够：
//     ① CreateSwapchain         —— OpenGL 里被完全隐藏的一叠图像
//     ② CreateGraphicsPipeline  —— OpenGL 里散落各处的全局状态，在这里被冻结成一个对象
//     ③ DrawFrame               —— OpenGL 帮你做的那些同步，在这里必须自己做
//
//   【本文件已经替你避掉的经典坑】（每一处现场都有详细注释，搜 "★" 都能找到）
//     1. 交换链格式优先选 sRGB —— 否则颜色发灰、和 OpenGL 版本对不上
//     2. 交换链尺寸必须原样采用 caps.currentExtent —— 不能自己算
//     3. 视口/裁剪用【动态状态】—— 缩放窗口时不用重建管线
//     4. cullMode = NONE —— 绕开"Vulkan 的 y 轴向下，导致绕序和 OpenGL 相反"
//     5. fence 必须等 acquire 成功之后再 reset —— 否则 acquire 失败那一帧会死等
//     6. 信号量按【帧号】索引，不能按 imageIndex 索引 —— 否则会复用到还在用的信号量
//   （另有：交换链重建前必须 vkDeviceWaitIdle；fence 初始状态必须 SIGNALED；
//     shader module 创建完管线就能销毁 —— 都在现场注释里）
// ============================================================

// ============================================================
// 一个匿名命名空间里的"空实现"：
//
// 上层（main.cpp）会调用 shader->BuildFromFiles(...) / mesh->SetData(...)，
// 所以 CreateShader()/CreateMesh() 不能返回 nullptr —— 那会直接崩。
// 这一版 Vulkan 后端还没有真正的 Shader / Mesh 实现，
// 所以返回"什么都不做"的占位对象：调用全部安全，只是没有效果。
// 三角形是由本文件里那条固定的管线画出来的，和这些占位对象无关。
//
// TODO: 将来拆成 Render/Vulkan/VulkanShader.{h,cpp} + VulkanMesh.{h,cpp}，
//       和 OpenGL 那边的 OpenGLShader/OpenGLMesh 一一对应。
// ============================================================
namespace {

class NullShader final : public IShader {
    public:
        bool BuildFromFiles(const std::string&, const std::string&) override { return true; }
        bool BuildFromSource(const std::string&, const std::string&) override { return true; }
        bool BuildFromShaderAsset(const std::string&, const std::string&) override { return true; }
        unsigned int GetID() const override { return 0; }   // Vulkan 没有 "program id" 这种概念
        void Use() override {}
        void SetMatrix(const glm::mat4&, const glm::mat4&, const glm::mat4&) override {}
        void SetLight(const glm::vec3&, const glm::vec3&) override {}
        void SetCamera(const glm::vec3&) override {}
        void SetMat4(const std::string&, const glm::mat4&) override {}
        void SetInt (const std::string&, int) override {}
        void SetVec2(const std::string&, const glm::vec2&) override {}
        void SetVec3(const std::string&, const glm::vec3&) override {}
};

class NullMesh final : public IMesh {
    public:
        void SetData(const float*, int, const unsigned int*, int) override {}
        void SetData(const ObjMeshData&) override {}
        void Draw() const override {}
};

// SPIR-V 文件开头一定是这个魔数（小端序的字节序列是 03 02 23 07）
constexpr uint32_t kSpirvMagic = 0x07230203u;

} // namespace

// ============================================================
// 构造 / 析构
// ============================================================
VulkanRenderer::VulkanRenderer(int width, int height)
    : WINDOW_WIDTH(width), WINDOW_HEIGHT(height)
{
    // 默认清屏色（和 OpenGLRenderer 的默认值保持一致，方便两边对照）
    mClearValue.color.float32[0] = 0.25f;
    mClearValue.color.float32[1] = 0.25f;
    mClearValue.color.float32[2] = 0.25f;
    mClearValue.color.float32[3] = 1.0f;
}

VulkanRenderer::~VulkanRenderer() {
    // ★ Vulkan 对象的销毁顺序基本是创建顺序的逆序，而且必须在窗口/实例销毁之前。
    //   Cleanup() 是幂等的，所以 WindowTerminate() 调过一次也没关系。
    Cleanup();
}

// ============================================================
// 【第 1 步】窗口：注意这里【不创建任何 OpenGL 上下文】
//
// ★ GLFW_CLIENT_API = GLFW_NO_API 是关键：
//   不加这个 hint 的话，GLFW 会去创建一个 OpenGL 上下文（Windows 上会加载 opengl32），
//   而 Vulkan 不需要它 —— 而且一个系统里同时用两套也没必要。
//   Vulkan 的设备是 vkCreateInstance 自己建的，和窗口的"上下文"无关；
//   窗口只负责"把画面显示出去"（靠 VkSurfaceKHR 对接）。
// ============================================================
bool VulkanRenderer::CreateWindow() {
    if (!glfwInit()) {
        std::cerr << "[VulkanRenderer] glfwInit 失败" << std::endl;
        return false;
    }

    // 先确认这太机器真的有可用的 Vulkan（GLFW 会去问 loader 和驱动）
    if (!glfwVulkanSupported()) {
        std::cerr << "[VulkanRenderer] GLFW 报告本机不支持 Vulkan（缺 loader 或显卡驱动）" << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);   // ★ 不要 OpenGL 上下文
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    mWindow = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Lynn Renderer (Vulkan)", nullptr, nullptr);
    if (!mWindow) {
        std::cerr << "[VulkanRenderer] 创建窗口失败" << std::endl;
        return false;
    }
    return true;
}

// ============================================================
// 【第 2 步】VkInstance
//
// ★ 需要哪些扩展不靠自己猜：GLFW 知道各个平台要什么
//   （Windows 上是 VK_KHR_surface + VK_KHR_win32_surface），
//   用 glfwGetRequiredInstanceExtensions 拿就行。
//
// ★ 这里【不开】validation layer：
//   它需要安装 Vulkan SDK 的层文件（VK_LAYER_KHRONOS_validation），
//   本机没装 SDK，开了会直接失败。等你装了 SDK，把下面注释那段打开即可
//   —— 强烈建议以后一直开着，Vulkan 的错误基本都靠它报出来。
// ============================================================
bool VulkanRenderer::CreateInstance() {
    VkApplicationInfo app{};
    app.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName   = "OpenGLRenderer";     // 同一个 exe，名字不变
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName        = "LynnRenderer";
    app.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion         = VK_API_VERSION_1_0;   // 只要求 1.0，兼容性最好

    uint32_t extCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extCount);
    if (!extensions) {
        std::cerr << "[VulkanRenderer] glfwGetRequiredInstanceExtensions 返回空" << std::endl;
        return false;
    }

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = extCount;
    ci.ppEnabledExtensionNames = extensions;
    ci.enabledLayerCount       = 0;      // 见上面的说明
    ci.ppEnabledLayerNames     = nullptr;

    const VkResult r = vkCreateInstance(&ci, nullptr, &mInstance);
    if (r != VK_SUCCESS) {
        std::cerr << "[VulkanRenderer] vkCreateInstance 失败，VkResult = " << r << std::endl;
        return false;
    }
    return true;
}

// ============================================================
// 【第 3 步】VkSurfaceKHR —— "画到哪个窗口上"
//
// ★ 为什么用 GLFW 创建而不是自己写 Win32 代码：
//   自己写要建 HINSTANCE/HWND、填 VkWin32SurfaceCreateInfoKHR，
//   换平台（Linux 的 XCB/Wayland、macOS 的 Metal）就得重写一遍。
//   GLFW 已经把这层封装好了 —— 这正是"用 GLFW 而不是裸 Win32"的价值。
// ============================================================
bool VulkanRenderer::CreateSurface() {
    const VkResult r = glfwCreateWindowSurface(mInstance, mWindow, nullptr, &mSurface);
    if (r != VK_SUCCESS) {
        std::cerr << "[VulkanRenderer] glfwCreateWindowSurface 失败，VkResult = " << r << std::endl;
        return false;
    }
    return true;
}

// ============================================================
// 队列族：Vulkan 不保证"渲染"和"显示"是同一个队列
//
// graphics 用来画，present 用来 vkQueuePresentKHR。
// 很多显卡（含 NVIDIA）是同一个队列族兼任两者，但【标准写法必须分别查】：
//   - 支持渲染：queueFlags 里有 VK_QUEUE_GRAPHICS_BIT
//   - 支持显示：vkGetPhysicalDeviceSurfaceSupportKHR 返回 VK_TRUE
// ============================================================
VulkanRenderer::QueueFamilies VulkanRenderer::QueryQueueFamilies(VkPhysicalDevice dev) const {
    QueueFamilies out;

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, props.data());

    for (uint32_t i = 0; i < count; ++i) {
        if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && out.graphics == 0xFFFFFFFFu) {
            out.graphics = i;
        }
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, mSurface, &presentSupport);
        if (presentSupport && out.present == 0xFFFFFFFFu) {
            out.present = i;
        }
        if (out.Complete()) break;
    }
    return out;
}

// ============================================================
// 交换链支持情况：能力 / 格式 / 呈现模式
//
// ★ 为什么要缓存 presentModes，而不只查"支不支持 VK_KHR_swapchain"：
//   有些设备报了扩展却没有任何可用的呈现模式 —— 只看扩展名会挑到这种设备，
//   然后在创建交换链时才失败。所以判据是"扩展 && formats 非空 && presentModes 非空"。
// ============================================================
VulkanRenderer::SwapchainSupport VulkanRenderer::QuerySwapchainSupport(VkPhysicalDevice dev) const {
    SwapchainSupport s;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, mSurface, &s.caps);

    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, mSurface, &n, nullptr);
    if (n) { s.formats.resize(n); vkGetPhysicalDeviceSurfaceFormatsKHR(dev, mSurface, &n, s.formats.data()); }

    n = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, mSurface, &n, nullptr);
    if (n) { s.presentModes.resize(n); vkGetPhysicalDeviceSurfacePresentModesKHR(dev, mSurface, &n, s.presentModes.data()); }

    return s;
}

// ============================================================
// 【第 4 步】选一张物理设备（显卡）
//
// ★ 一定要检查三件事，缺一不可：
//   ① 有能画图的队列族，而且它也能显示（或者另有一个能显示的）
//   ② 支持 VK_KHR_swapchain 扩展
//   ③ 交换链真的有可用的格式和呈现模式（上面那个坑）
//   只看"是个显卡"就选，会在后面某一步莫名其妙失败。
// ============================================================
bool VulkanRenderer::PickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(mInstance, &count, nullptr);
    if (count == 0) {
        std::cerr << "[VulkanRenderer] 找不到任何支持 Vulkan 的显卡" << std::endl;
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(mInstance, &count, devices.data());

    int bestScore = -1;

    for (VkPhysicalDevice dev : devices) {
        const QueueFamilies qf = QueryQueueFamilies(dev);
        if (!qf.Complete()) continue;

        // 交换链扩展
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());
        bool hasSwapchain = false;
        for (const auto& e : exts) {
            if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) { hasSwapchain = true; break; }
        }
        if (!hasSwapchain) continue;

        // 交换链真的可用（见 QuerySwapchainSupport 的说明）
        if (!QuerySwapchainSupport(dev).Usable()) continue;

        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(dev, &p);

        // 评分：独显优先（笔记本上核显也支持 Vulkan，但性能差很多）
        int score = 0;
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;
        score += static_cast<int>(p.limits.maxImageDimension2D) / 1024;   // 显存/能力的一个粗略代理

        if (score > bestScore) {
            bestScore        = score;
            mPhysicalDevice  = dev;
            mGraphicsFamily  = qf.graphics;
            mPresentFamily   = qf.present;
        }
    }

    if (mPhysicalDevice == VK_NULL_HANDLE) {
        std::cerr << "[VulkanRenderer] 没有找到满足条件的显卡（需要 graphics 队列 + swapchain 扩展）" << std::endl;
        return false;
    }

    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &p);
    std::cout << "[VulkanRenderer] 选中显卡: " << p.deviceName
              << "  (Vulkan " << VK_VERSION_MAJOR(p.apiVersion) << "."
              << VK_VERSION_MINOR(p.apiVersion) << "." << VK_VERSION_PATCH(p.apiVersion) << ")"
              << "  graphicsFamily=" << mGraphicsFamily
              << "  presentFamily=" << mPresentFamily << std::endl;
    return true;
}

// ============================================================
// 【第 5 步】逻辑设备 + 取队列
//
// ★ 两个坑：
//   ① graphics 和 present 可能是【同一个队列族】，这时只能创建【一个】队列
//      —— 用 std::set 去重，否则 vkCreateDevice 会因为"同一族重复请求"而失败。
//   ② 队列的优先级（pQueuePriorities）必须给一个合法指针，
//      值为 0.0~1.0，这里用 1.0（最高）。
// ============================================================
bool VulkanRenderer::CreateLogicalDevice() {
    const float priority = 1.0f;
    std::set<uint32_t> uniqueFamilies = { mGraphicsFamily, mPresentFamily };

    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t fam : uniqueFamilies) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = fam;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &priority;
        queueInfos.push_back(qi);
    }

    VkPhysicalDeviceFeatures features{};   // 全关：三角形用不到任何可选特性

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = static_cast<uint32_t>(queueInfos.size());
    ci.pQueueCreateInfos       = queueInfos.data();
    ci.pEnabledFeatures        = &features;
    ci.enabledExtensionCount   = 1;
    ci.ppEnabledExtensionNames = deviceExtensions;

    if (vkCreateDevice(mPhysicalDevice, &ci, nullptr, &mDevice) != VK_SUCCESS) {
        std::cerr << "[VulkanRenderer] vkCreateDevice 失败" << std::endl;
        return false;
    }

    // 队列句柄由设备"生出来"，不是单独创建的
    vkGetDeviceQueue(mDevice, mGraphicsFamily, 0, &mGraphicsQueue);
    vkGetDeviceQueue(mDevice, mPresentFamily,  0, &mPresentQueue);
    return true;
}

// ============================================================
// 交换链的三个"选哪个"策略
// ============================================================

// 首选 BGRA8 + sRGB；否则退而求其次挑第一个可用的。
// ★ 为什么优先 sRGB：它是"会自动做 gamma 编码"的格式，
//   颜色看起来才对。非 sRGB 的格式要自己在着色器里做 gamma，否则画面发灰。
VkSurfaceFormatKHR VulkanRenderer::ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const {
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats[0];
}

// 呈現模式：MAILBOX(三缓冲，延迟低) > FIFO(垂直同步，永远可用) > 第一个。
// ★ FIFO 是规范保证一定支持的，所以兜底一定能用。
VkPresentModeKHR VulkanRenderer::ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes) const {
    for (VkPresentModeKHR m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

// 交换链尺寸
// ★ caps.currentExtent 是"当前窗口告诉你该用多大"：
//   如果它不是 UINT32_MAX，就必须【原样采用】——不能自己算，否则会不一致。
//   只有它是 UINT32_MAX 时（某些平台）才由我们自己 clamp 窗口尺寸。
VkExtent2D VulkanRenderer::ChooseExtent(const VkSurfaceCapabilitiesKHR& caps) const {
    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        return caps.currentExtent;
    }
    int w = 0, h = 0;
    glfwGetFramebufferSize(mWindow, &w, &h);
    VkExtent2D e{};
    e.width  = std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width,  caps.maxImageExtent.width);
    e.height = std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height);
    return e;
}

// ============================================================
// 【第 6 步】交换链
//
// ★ imageCount 的取法：minImageCount + 1（多一张，避免等 GPU 空转），
//   并且不能超过 maxImageCount（0 表示"不限制"）。
// ★ 若 graphics != present，sharingMode 要用 CONCURRENT 并把两个族都写上；
//   否则用 EXCLUSIVE（性能更好，但必须确实是同一个族）。
//   用 CONCURRENT 的代价是驱动可能少一点优化，换来的是"永远正确"。
// ============================================================
bool VulkanRenderer::CreateSwapchain() {
    const SwapchainSupport support = QuerySwapchainSupport(mPhysicalDevice);
    const VkSurfaceFormatKHR format = ChooseSurfaceFormat(support.formats);
    const VkPresentModeKHR   mode   = ChoosePresentMode(support.presentModes);
    const VkExtent2D         extent = ChooseExtent(support.caps);

    uint32_t imageCount = support.caps.minImageCount + 1;
    if (support.caps.maxImageCount > 0 && imageCount > support.caps.maxImageCount) {
        imageCount = support.caps.maxImageCount;
    }

    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(support.caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        // 极少数平台不支持 OPAQUE，那就退到 INHERIT
        alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = mSurface;
    ci.minImageCount    = imageCount;
    ci.imageFormat      = format.format;
    ci.imageColorSpace  = format.colorSpace;
    ci.imageExtent      = extent;
    ci.imageArrayLayers = 1;                                  // 非立体渲染
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // 只当颜色附件画
    ci.preTransform     = support.caps.currentTransform;       // 别自己"转正"，按平台给的值
    ci.compositeAlpha   = alpha;
    ci.presentMode      = mode;
    ci.clipped          = VK_TRUE;                             // 被挡住的像素不关心
    ci.oldSwapchain     = VK_NULL_HANDLE;                      // 重建时也已经先销毁旧的

    const uint32_t families[2] = { mGraphicsFamily, mPresentFamily };
    if (mGraphicsFamily != mPresentFamily) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = families;
    } else {
        ci.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
        ci.queueFamilyIndexCount = 0;
        ci.pQueueFamilyIndices   = nullptr;
    }

    if (vkCreateSwapchainKHR(mDevice, &ci, nullptr, &mSwapchain) != VK_SUCCESS) {
        std::cerr << "[VulkanRenderer] vkCreateSwapchainKHR 失败" << std::endl;
        return false;
    }
    mSwapchainFormat = format.format;
    mSwapchainExtent = extent;

    // ★ 交换链里的图像是"驱动拥有"的，我们只拿到句柄，不能自己创建/销毁
    uint32_t count = 0;
    vkGetSwapchainImagesKHR(mDevice, mSwapchain, &count, nullptr);
    mSwapchainImages.resize(count);
    vkGetSwapchainImagesKHR(mDevice, mSwapchain, &count, mSwapchainImages.data());

    std::cout << "[VulkanRenderer] 交换链: " << extent.width << "x" << extent.height
              << "  图像数=" << count
              << "  format=" << format.format
              << "  presentMode=" << (mode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX" : "FIFO") << std::endl;
    return true;
}

// ============================================================
// 【第 7 步】图像视图（VkImageView）
//
// ★ VkImage 只是"显存里的一块"，要当附件/纹理用必须先包一层 VkImageView
//   告诉驱动"这块内存按什么格式、哪一层、哪一部分来看"。
//   这一步几乎总是"一模一样地包一遍"，但不能省。
// ============================================================
bool VulkanRenderer::CreateImageViews() {
    mSwapchainImageViews.resize(mSwapchainImages.size());

    for (size_t i = 0; i < mSwapchainImages.size(); ++i) {
        VkImageViewCreateInfo ci{};
        ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image                           = mSwapchainImages[i];
        ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ci.format                          = mSwapchainFormat;
        ci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;   // 不做通道重排
        ci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        ci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        ci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.baseMipLevel   = 0;
        ci.subresourceRange.levelCount     = 1;
        ci.subresourceRange.baseArrayLayer = 0;
        ci.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(mDevice, &ci, nullptr, &mSwapchainImageViews[i]) != VK_SUCCESS) {
            std::cerr << "[VulkanRenderer] vkCreateImageView 失败" << std::endl;
            return false;
        }
    }
    return true;
}

// ============================================================
// 【第 8 步】Render Pass —— 描述"这一趟渲染要用哪些附件、怎么处理"
//
// ★ 这是 Vulkan 和 OpenGL 差别最大、也是最有价值的一个概念：
//   OpenGL 里"渲染目标、清屏、格式"全是全局状态，随时可变；
//   Vulkan 把"一趟渲染的附件布局 + 读写时机"在【管线创建时就固定下来】，
//   驱动因此可以提前把 GPU 的工作安排好（这是它性能更好的原因之一）。
//
// ★ layout 的含义（这里只有一张图，很简单）：
//   initialLayout = UNDEFINED   ：不用管进来时是什么样，我全清掉
//   finalLayout   = PRESENT_SRC ：画完必须变成"可以拿去显示"的状态
//   中间驱动会自动插入"布局转换"。
//
// ★ subpass dependency 是必须的：
//   它告诉驱动"颜色附件的写入要等 COLOR_ATTACHMENT_OUTPUT 阶段" ——
//   没有它，驱动可能在我们还没开始画的时候就把图像交给显示系统，
//   典型症状是【图像撕裂/闪烁】（尤其开了 validation layer 会直接报错）。
// ============================================================
bool VulkanRenderer::CreateRenderPass() {
    VkAttachmentDescription color{};
    color.format         = mSwapchainFormat;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;          // 不开 MSAA
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;    // ★ 这就是"清屏"发生的地方
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;   // 画完要留着显示
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;   // "这一趟渲染之外"
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments    = &color;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;

    if (vkCreateRenderPass(mDevice, &ci, nullptr, &mRenderPass) != VK_SUCCESS) {
        std::cerr << "[VulkanRenderer] vkCreateRenderPass 失败" << std::endl;
        return false;
    }
    return true;
}

// ============================================================
// 工具：把文件整个读成字节数组（SPIR-V 是二进制，不能用文本方式读）
//
// ★ 顺手校验 SPIR-V 魔数：如果路径写错、文件被当文本存过、或者忘了用 -V 编译，
//   都在这里立刻报出来，而不是等 vkCreateShaderModule 返回一个含糊的错误码。
// ============================================================
std::vector<char> VulkanRenderer::ReadFileBytes(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[VulkanRenderer] 打不开文件: " << path << std::endl;
        return {};
    }
    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), static_cast<std::streamsize>(size));
    file.close();

    if (size < 4 || (size % 4) != 0) {
        std::cerr << "[VulkanRenderer] " << path << " 不是合法的 SPIR-V（大小 " << size << " 不是 4 的倍数）" << std::endl;
        return {};
    }
    uint32_t magic = 0;
    std::memcpy(&magic, buf.data(), 4);
    if (magic != kSpirvMagic) {
        std::cerr << "[VulkanRenderer] " << path << " 的魔数不对（0x" << std::hex << magic
                  << "）—— 这不是 SPIR-V。用 glslang 编译时别忘了 -V" << std::dec << std::endl;
        return {};
    }
    return buf;
}

VkShaderModule VulkanRenderer::CreateShaderModule(const std::vector<char>& code) const {
    if (code.empty()) return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());   // SPIR-V 以 4 字节为单位

    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(mDevice, &ci, nullptr, &mod) != VK_SUCCESS) {
        std::cerr << "[VulkanRenderer] vkCreateShaderModule 失败" << std::endl;
        return VK_NULL_HANDLE;
    }
    return mod;
}

// ============================================================
// 【第 9 步】图形管线 —— Vulkan 的"大对象"
//
// ★ 它就是 OpenGL 那一堆 glEnable/glBlendFunc/glDepthFunc/视口/着色器程序的
//   【打包冻结版】：所有状态在这里一次写完，运行时不可改（要改就重建管线）。
//   这就是为什么 Vulkan 里没有 EnableRendererFeature() 这种东西。
//
// ★ 本管线刻意做到最小：
//   - 顶点输入：0 个 binding、0 个 attribute（顶点数据全靠 gl_VertexIndex 在着色器里造）
//   - 视口/裁剪：用【动态状态】(VK_DYNAMIC_STATE_VIEWPORT/SCISSOR)
//       → 好处是窗口缩放时不用重建管线，只要在命令缓冲里重设一下即可
//   - 剔面：关掉(CULL_MODE_NONE)
//       → 顺便绕开"Vulkan 的 y 轴向下导致三角形绕序和 OpenGL 相反"这个经典坑
//   - 混合：关（直接覆盖）
//   - 深度：没有深度附件，相关状态全部忽略
// ============================================================
bool VulkanRenderer::CreateGraphicsPipeline() {
    const std::string base = std::string(PROJECT_SOURCE_DIR) + "/src/shaders/";
    const std::vector<char> vertCode = ReadFileBytes(base + "vulkan_triangle.vert.spv");
    const std::vector<char> fragCode = ReadFileBytes(base + "vulkan_triangle.frag.spv");
    if (vertCode.empty() || fragCode.empty()) return false;

    VkShaderModule vertMod = CreateShaderModule(vertCode);
    VkShaderModule fragMod = CreateShaderModule(fragCode);
    if (vertMod == VK_NULL_HANDLE || fragMod == VK_NULL_HANDLE) return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName  = "main";                 // ★ SPIR-V 里的入口函数名，必须和着色器一致
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName  = "main";

    // ---- 顶点输入：空 ----
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 0;
    vertexInput.pVertexBindingDescriptions      = nullptr;
    vertexInput.vertexAttributeDescriptionCount = 0;
    vertexInput.pVertexAttributeDescriptions    = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // ★ 用动态视口/裁剪：这里只声明"有几个"，实际值在命令缓冲里给
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports    = nullptr;    // 动态
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = nullptr;    // 动态

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.depthClampEnable        = VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode             = VK_POLYGON_MODE_FILL;
    raster.lineWidth               = 1.0f;
    raster.cullMode                = VK_CULL_MODE_NONE;             // 见上面的说明
    raster.frontFace               = VK_FRONT_FACE_CLOCKWISE;       // 剔面关着，这里只是写清楚
    raster.depthBiasEnable         = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample.sampleShadingEnable  = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable         = VK_FALSE;   // 不混合，直接写入
    blendAttachment.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.logicOpEnable   = VK_FALSE;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAttachment;

    const VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dynStates;

    // pipeline layout：这一版【没有】任何 uniform / descriptor，所以是空的
    VkPipelineLayoutCreateInfo layoutCi{};
    layoutCi.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCi.setLayoutCount         = 0;
    layoutCi.pSetLayouts            = nullptr;
    layoutCi.pushConstantRangeCount = 0;
    if (vkCreatePipelineLayout(mDevice, &layoutCi, nullptr, &mPipelineLayout) != VK_SUCCESS) {
        std::cerr << "[VulkanRenderer] vkCreatePipelineLayout 失败" << std::endl;
        return false;
    }

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount          = 2;
    ci.pStages             = stages;
    ci.pVertexInputState   = &vertexInput;
    ci.pInputAssemblyState = &inputAssembly;
    ci.pViewportState      = &viewportState;
    ci.pRasterizationState = &raster;
    ci.pMultisampleState   = &multisample;
    ci.pColorBlendState    = &blend;
    ci.pDynamicState       = &dynamic;
    ci.layout              = mPipelineLayout;
    ci.renderPass          = mRenderPass;
    ci.subpass             = 0;

    const VkResult r = vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &ci, nullptr, &mPipeline);

    // ★ shader module 的使命已经结束：管线里存的是编译后的结果，
    //   这个"外壳"可以立刻销毁（这是常见做法，也避免忘记释放）
    vkDestroyShaderModule(mDevice, vertMod, nullptr);
    vkDestroyShaderModule(mDevice, fragMod, nullptr);

    if (r != VK_SUCCESS) {
        std::cerr << "[VulkanRenderer] vkCreateGraphicsPipelines 失败，VkResult = " << r << std::endl;
        return false;
    }
    return true;
}

// ============================================================
// 【第 10 步】Framebuffer —— 把"附件"（图像视图）绑成一个具体的渲染目标
//
// ★ Render Pass 描述的是【格式和布局】（抽象的），
//   Framebuffer 才是把【具体的图像视图】填进去（具体的）。
//   所以：有几张交换链图像，就要有几个 framebuffer。
// ============================================================
bool VulkanRenderer::CreateFramebuffers() {
    mFramebuffers.resize(mSwapchainImageViews.size(), VK_NULL_HANDLE);

    for (size_t i = 0; i < mSwapchainImageViews.size(); ++i) {
        VkImageView attachments[] = { mSwapchainImageViews[i] };

        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = mRenderPass;
        ci.attachmentCount = 1;
        ci.pAttachments    = attachments;
        ci.width           = mSwapchainExtent.width;
        ci.height          = mSwapchainExtent.height;
        ci.layers          = 1;

        if (vkCreateFramebuffer(mDevice, &ci, nullptr, &mFramebuffers[i]) != VK_SUCCESS) {
            std::cerr << "[VulkanRenderer] vkCreateFramebuffer 失败" << std::endl;
            return false;
        }
    }
    return true;
}

// ============================================================
// 【第 11 步】命令池 / 命令缓冲
//
// ★ 顺序：几乎每个 Vulkan 操作都要"记录到命令缓冲里"再提交，
//   而命令缓冲必须从命令池里分配 —— 所以池要早于缓冲。
//
// ★ VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT：
//   允许每帧单独 reset 某一条命令缓冲（否则只能整池重置）。
//   我们要"每帧重新录制"，所以需要它。
// ============================================================
bool VulkanRenderer::CreateCommandPool() {
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = mGraphicsFamily;   // ★ 池属于某个队列族，必须和后面提交的队列一致

    if (vkCreateCommandPool(mDevice, &ci, nullptr, &mCommandPool) != VK_SUCCESS) {
        std::cerr << "[VulkanRenderer] vkCreateCommandPool 失败" << std::endl;
        return false;
    }
    return true;
}

bool VulkanRenderer::CreateCommandBuffers() {
    mCommandBuffers.resize(kMaxFramesInFlight, VK_NULL_HANDLE);

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = mCommandPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;   // 可直接提交的级别
    ai.commandBufferCount = static_cast<uint32_t>(mCommandBuffers.size());

    if (vkAllocateCommandBuffers(mDevice, &ai, mCommandBuffers.data()) != VK_SUCCESS) {
        std::cerr << "[VulkanRenderer] vkAllocateCommandBuffers 失败" << std::endl;
        return false;
    }
    return true;
}

// ============================================================
// 【第 12 步】同步对象：每帧在飞一套
//
//   mImageAvailable[frame]  : GPU 说"交换链图像拿到了，可以开始画"
//   mRenderFinished[frame]  : GPU 说"画完了，可以显示了"
//   mInFlightFences[frame]  : CPU 等"这一帧的 GPU 工作做完了"，避免覆盖正在用的缓冲
//
// ★ fence 的初始状态必须是【有信号】(SIGNALED)：
//   否则第一次 vkWaitForFences 会永久卡死（永远等一个永远不会来的信号）。
// ============================================================
bool VulkanRenderer::CreateSyncObjects() {
    mImageAvailable.resize(kMaxFramesInFlight);
    mRenderFinished.resize(kMaxFramesInFlight);
    mInFlightFences.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;   // ★ 见上面的说明

    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        if (vkCreateSemaphore(mDevice, &si, nullptr, &mImageAvailable[i]) != VK_SUCCESS ||
            vkCreateSemaphore(mDevice, &si, nullptr, &mRenderFinished[i]) != VK_SUCCESS ||
            vkCreateFence(mDevice, &fi, nullptr, &mInFlightFences[i]) != VK_SUCCESS) {
            std::cerr << "[VulkanRenderer] 创建同步对象失败" << std::endl;
            return false;
        }
    }
    return true;
}

// ============================================================
// 交换链重建（窗口缩放 / surface 失效）
// ============================================================
void VulkanRenderer::DestroySwapchainResources() {
    for (VkFramebuffer fb : mFramebuffers) {
        if (fb) vkDestroyFramebuffer(mDevice, fb, nullptr);
    }
    mFramebuffers.clear();

    for (VkImageView v : mSwapchainImageViews) {
        if (v) vkDestroyImageView(mDevice, v, nullptr);
    }
    mSwapchainImageViews.clear();

    mSwapchainImages.clear();   // ★ 这些 VkImage 是驱动的，只清数组，不能销毁
}

bool VulkanRenderer::CreateSwapchainResources() {
    return CreateImageViews() && CreateFramebuffers();
}

bool VulkanRenderer::RecreateSwapchain() {
    // 窗口被最小化时 framebuffer 尺寸是 0 —— 此时不能创建交换链，等它恢复
    int w = 0, h = 0;
    glfwGetFramebufferSize(mWindow, &w, &h);
    while (w == 0 || h == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(mWindow, &w, &h);
    }

    // ★ 必须等 GPU 空转：正在被使用的交换链图像/命令缓冲不能动
    vkDeviceWaitIdle(mDevice);

    const VkFormat oldFormat = mSwapchainFormat;

    DestroySwapchainResources();
    if (mSwapchain) {
        vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
        mSwapchain = VK_NULL_HANDLE;
    }

    if (!CreateSwapchain()) return false;
    if (!CreateSwapchainResources()) return false;

    // ★ 已知局限：如果新交换链的格式和之前不一样（比如窗口被拖到另一个显示器，
    //   或者 HDR 开关变了），那么 render pass / pipeline 里写死的格式就过期了，
    //   正确做法是把它们一起重建。这里只警告 —— 因为窗口缩放不会改格式。
    if (mSwapchainFormat != oldFormat) {
        std::cerr << "[VulkanRenderer] 警告：交换链格式变了（" << oldFormat << " -> " << mSwapchainFormat
                  << "），render pass / pipeline 需要重建（当前实现未处理）" << std::endl;
    }
    return true;
}

// ============================================================
// 【第 13 步】每帧
// ============================================================
void VulkanRenderer::RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &begin);

    // ---- 进入 render pass：顺便完成"清屏" ----
    //   clearValueCount = 1 对应 render pass 里那唯一一个附件
    VkRenderPassBeginInfo rp{};
    rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass        = mRenderPass;
    rp.framebuffer       = mFramebuffers[imageIndex];   // ★ 用"拿到的这张图"对应的 framebuffer
    rp.renderArea.offset = { 0, 0 };
    rp.renderArea.extent = mSwapchainExtent;
    rp.clearValueCount   = 1;
    rp.pClearValues      = &mClearValue;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline);

    // ---- 动态状态：视口和裁剪矩形（管线里声明为动态，所以必须在这里设）----
    //   ★ y 轴：这里用 0..height 的"常规"写法；Vulkan 的裁剪空间 y 向下，
    //     所以更大的 y 在更下面，这和应用层理解的屏幕坐标一致（不需要翻转）。
    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(mSwapchainExtent.width);
    viewport.height   = static_cast<float>(mSwapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = mSwapchainExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // ---- 画！3 个顶点，没有顶点缓冲（顶点在着色器里靠 gl_VertexIndex 造）----
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        std::cerr << "[VulkanRenderer] vkEndCommandBuffer 失败" << std::endl;
    }
}

void VulkanRenderer::DrawFrame() {
    if (!mDevice) return;

    // ---- 0) 窗口尺寸变了就重建交换链 ----
    //   不去注册 GLFW 的 resize 回调，而是每帧直接比较尺寸 —— 少一处跨文件的状态传递，
    //   而且"最小化"这种情况也能顺手处理掉（尺寸为 0 就直接不画）。
    int w = 0, h = 0;
    glfwGetFramebufferSize(mWindow, &w, &h);
    if (w <= 0 || h <= 0) return;   // 最小化：什么都不做
    if (static_cast<uint32_t>(w) != mSwapchainExtent.width ||
        static_cast<uint32_t>(h) != mSwapchainExtent.height) {
        if (!RecreateSwapchain()) return;
    }

    // ---- 1) 等这一帧上一轮的 GPU 工作做完（否则会覆盖它还在用的命令缓冲）----
    vkWaitForFences(mDevice, 1, &mInFlightFences[mCurrentFrame], VK_TRUE, UINT64_MAX);

    // ---- 2) 从交换链取一张可用的图像 ----
    uint32_t imageIndex = 0;
    const VkResult acquire = vkAcquireNextImageKHR(
        mDevice, mSwapchain, UINT64_MAX,
        mImageAvailable[mCurrentFrame], VK_NULL_HANDLE, &imageIndex);

    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        // 交换链和 surface 不匹配了（比如刚缩放过）→ 重建后重来
        RecreateSwapchain();
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        std::cerr << "[VulkanRenderer] vkAcquireNextImageKHR 失败: " << acquire << std::endl;
        return;
    }

    // ★ fence 必须等到"acquire 成功之后"才重置。
    //   如果在 acquire 之前就重置，而 acquire 因为 out-of-date 提前返回了，
    //   这一帧的 fence 就永远处于"无信号"状态 → 下一轮 vkWaitForFences 死等。
    vkResetFences(mDevice, 1, &mInFlightFences[mCurrentFrame]);

    // ---- 3) 重新录制命令缓冲 ----
    vkResetCommandBuffer(mCommandBuffers[mCurrentFrame], 0);
    RecordCommandBuffer(mCommandBuffers[mCurrentFrame], imageIndex);

    // ---- 4) 提交 ----
    //   ★ 信号量的配对关系：
    //     等 mImageAvailable（图像可取）→ 到 COLOR_ATTACHMENT_OUTPUT 阶段才能开始写颜色
    //     发 mRenderFinished（画完了）→ 给 vkQueuePresentKHR 等
    //   ★ 信号量按【帧号】索引，不要按 imageIndex 索引 ——
    //     按 image 索引会复用到"上一帧还在用的"信号量，validation layer 会直接报错。
    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = { mImageAvailable[mCurrentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { mRenderFinished[mCurrentFrame] };
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = waitSemaphores;
    submit.pWaitDstStageMask    = waitStages;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &mCommandBuffers[mCurrentFrame];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = signalSemaphores;

    if (vkQueueSubmit(mGraphicsQueue, 1, &submit, mInFlightFences[mCurrentFrame]) != VK_SUCCESS) {
        std::cerr << "[VulkanRenderer] vkQueueSubmit 失败" << std::endl;
        return;
    }

    // ---- 5) 显示（这就是 Vulkan 的"交换缓冲"）----
    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = signalSemaphores;
    present.swapchainCount     = 1;
    present.pSwapchains        = &mSwapchain;
    present.pImageIndices      = &imageIndex;

    const VkResult pres = vkQueuePresentKHR(mPresentQueue, &present);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
        RecreateSwapchain();
    }

    mCurrentFrame = (mCurrentFrame + 1) % kMaxFramesInFlight;
}

// ============================================================
// Init：严格的依赖顺序
// ============================================================
bool VulkanRenderer::Init() {
    if (!CreateWindow())       return false;
    if (!CreateInstance())     return false;
    if (!CreateSurface())      return false;
    if (!PickPhysicalDevice()) return false;
    if (!CreateLogicalDevice())return false;

    // 交换链必须在 render pass 之前：后者需要知道图像格式
    if (!CreateSwapchain())    return false;
    if (!CreateImageViews())   return false;
    if (!CreateRenderPass())   return false;
    if (!CreateGraphicsPipeline()) return false;
    if (!CreateFramebuffers()) return false;   // 需要 render pass + image views

    if (!CreateCommandPool())  return false;
    if (!CreateCommandBuffers()) return false;
    if (!CreateSyncObjects())  return false;

    std::cout << "[VulkanRenderer] 初始化完成，开始画三角形（3 个顶点，无顶点缓冲）" << std::endl;
    return true;
}

// ============================================================
// IRenderer 接口
// ============================================================
glm::vec2 VulkanRenderer::GetWindowSize() {
    if (!mWindow) return glm::vec2(0.0f);
    int w = 0, h = 0;
    glfwGetFramebufferSize(mWindow, &w, &h);
    return glm::vec2(static_cast<float>(w), static_cast<float>(h));
}

void* VulkanRenderer::GetWindow() {
    return mWindow;
}

void VulkanRenderer::SetClearColor(const glm::vec4& color) {
    // ★ Vulkan 没有"设置全局清屏色"这种东西：清屏是 render pass 的 loadOp=CLEAR 干的。
    //   所以这里只是把颜色存下来，等 RecordCommandBuffer 时当 clearValue 用。
    mClearValue.color.float32[0] = color.r;
    mClearValue.color.float32[1] = color.g;
    mClearValue.color.float32[2] = color.b;
    mClearValue.color.float32[3] = color.a;
}

void VulkanRenderer::Clear() {
    // 空实现：清屏已经由 render pass 的 loadOp 完成（见 SetClearColor 的说明）
}

bool VulkanRenderer::WindowShouldClose() {
    return mWindow ? (glfwWindowShouldClose(mWindow) != 0) : true;
}

void VulkanRenderer::PollEvents() {
    glfwPollEvents();
}

void VulkanRenderer::SwapBuffers() {
    // 空实现：呈现已经发生在 DrawFrame() 里的 vkQueuePresentKHR
    // （拆成两半的话，acquire/submit/present 的状态要跨函数保存，得不偿失）
}

void VulkanRenderer::EnableRendererFeature(BuiltInRendererFeatures) {
    // 空实现：Vulkan 没有"全局渲染状态"。深度测试/混合/剔面都写在 pipeline 里，
    // 想改只能重建管线（或者在 pipeline 里做成动态状态）。
}

void VulkanRenderer::DisableRendererFeature(BuiltInRendererFeatures) {
    // 同上
}

IShader* VulkanRenderer::CreateShader() {
    return new NullShader();   // 见文件顶部匿名命名空间的说明
}

IMesh* VulkanRenderer::CreateMesh() {
    return new NullMesh();
}

void VulkanRenderer::ExecuteRenderCommands(const std::vector<RenderCommand>& RenderingCommandQueue,
                                           const CameraData& RenderingCameraData) {
    // ★ 刻意忽略 RenderCommandQueue / CameraData：
    //   这一版的目标是"用 Vulkan 画出一个三角形"，三角形是焊死在裁剪空间的，
    //   不需要任何物体数据。参数留着是为了将来接上前向渲染时签名不用改。
    (void)RenderingCommandQueue;
    (void)RenderingCameraData;

    DrawFrame();
}

void VulkanRenderer::WindowTerminate() {
    Cleanup();
}

// ============================================================
// 清理：幂等，按创建顺序的逆序销毁
// ============================================================
void VulkanRenderer::Cleanup() {
    if (mDevice != VK_NULL_HANDLE) {
        // ★ 先等 GPU 空转，否则销毁正在被使用的对象会导致崩溃或设备丢失
        vkDeviceWaitIdle(mDevice);
    }

    // ---- 同步对象 ----
    for (VkSemaphore s : mImageAvailable) if (s) vkDestroySemaphore(mDevice, s, nullptr);
    for (VkSemaphore s : mRenderFinished) if (s) vkDestroySemaphore(mDevice, s, nullptr);
    for (VkFence     f : mInFlightFences) if (f) vkDestroyFence(mDevice, f, nullptr);
    mImageAvailable.clear(); mRenderFinished.clear(); mInFlightFences.clear();

    // ---- 命令池（它拥有命令缓冲，销毁池等于销毁里面的缓冲）----
    if (mCommandPool) { vkDestroyCommandPool(mDevice, mCommandPool, nullptr); mCommandPool = VK_NULL_HANDLE; }
    mCommandBuffers.clear();

    // ---- 管线 / 布局 / render pass ----
    if (mPipeline)       { vkDestroyPipeline(mDevice, mPipeline, nullptr);             mPipeline = VK_NULL_HANDLE; }
    if (mPipelineLayout) { vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr); mPipelineLayout = VK_NULL_HANDLE; }
    if (mRenderPass)     { vkDestroyRenderPass(mDevice, mRenderPass, nullptr);         mRenderPass = VK_NULL_HANDLE; }

    // ---- 交换链相关（framebuffer / image view / swapchain）----
    if (mDevice != VK_NULL_HANDLE) {
        DestroySwapchainResources();
        if (mSwapchain) { vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr); mSwapchain = VK_NULL_HANDLE; }
    }

    if (mDevice)   { vkDestroyDevice(mDevice, nullptr);     mDevice = VK_NULL_HANDLE; }
    if (mSurface)  { vkDestroySurfaceKHR(mInstance, mSurface, nullptr); mSurface = VK_NULL_HANDLE; }
    if (mInstance) { vkDestroyInstance(mInstance, nullptr); mInstance = VK_NULL_HANDLE; }

    mPhysicalDevice = VK_NULL_HANDLE;
    mGraphicsQueue  = VK_NULL_HANDLE;
    mPresentQueue   = VK_NULL_HANDLE;

    if (mWindow) { glfwDestroyWindow(mWindow); mWindow = nullptr; }
    glfwTerminate();
}
