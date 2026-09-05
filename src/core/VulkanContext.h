#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

// Owns the GLFW window + Vulkan instance/device/swapchain and the ImGui
// Vulkan backend. This is deliberately close to the standard
// imgui/examples/example_glfw_vulkan bootstrap, wrapped in a class so
// Application.cpp stays focused on app logic instead of boilerplate.
class VulkanContext {
public:
    bool Init(int width, int height, const char* title);
    void Shutdown();

    // Call once after Init(), before the main loop, to set up the ImGui
    // Vulkan backend (font upload, descriptor pool, render pass binding).
    bool InitImGuiBackend();
    void ShutdownImGuiBackend();

    // Split so extra passes (e.g. ShadowMap) can record into the same
    // command buffer BEFORE the swapchain render pass begins:
    //   AcquireAndBeginRecording() -> [other passes record here] ->
    //   BeginSwapchainRenderPass() -> [ImGui/UI record here] -> EndFrame()
    bool AcquireAndBeginRecording();
    void BeginSwapchainRenderPass();
    void EndFrame();

    // Convenience: old call sites that don't need extra passes.
    bool BeginFrame() { return AcquireAndBeginRecording() ? (BeginSwapchainRenderPass(), true) : false; }

    GLFWwindow* Window() const { return window_; }
    VkInstance Instance() const { return instance_; }
    VkPhysicalDevice PhysicalDevice() const { return physicalDevice_; }
    VkDevice Device() const { return device_; }
    VkQueue GraphicsQueue() const { return graphicsQueue_; }
    uint32_t GraphicsQueueFamily() const { return graphicsQueueFamily_; }
    VkCommandPool CommandPool() const { return commandPool_; } // usable for single-time transfer/setup commands too
    VkRenderPass RenderPass() const { return renderPass_; }
    VkCommandBuffer CurrentCommandBuffer() const { return commandBuffers_[currentFrame_]; }
    VkDescriptorPool DescriptorPool() const { return descriptorPool_; }
    uint32_t ImageCount() const { return static_cast<uint32_t>(swapchainImages_.size()); }
    int FramebufferWidth() const { return fbWidth_; }
    int FramebufferHeight() const { return fbHeight_; }
    bool ShouldClose() const;

private:
    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    std::vector<VkFramebuffer> framebuffers_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    static constexpr int kMaxFramesInFlight = 2;
    VkSemaphore imageAvailable_[kMaxFramesInFlight]{};
    VkSemaphore renderFinished_[kMaxFramesInFlight]{};
    VkFence inFlightFence_[kMaxFramesInFlight]{};
    int currentFrame_ = 0;
    uint32_t currentImageIndex_ = 0;

    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    int fbWidth_ = 0, fbHeight_ = 0;
    bool imguiBackendInitialized_ = false;

    bool createInstance(const char* title);
    bool pickPhysicalDeviceAndQueue();
    bool createLogicalDevice();
    bool createSwapchain();
    void destroySwapchain();
    bool createRenderPass();
    bool createFramebuffers();
    bool createCommandPoolAndBuffers();
    bool createSyncObjects();
    bool createDescriptorPool();
    void recreateSwapchain();
};
