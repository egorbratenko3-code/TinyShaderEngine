#include "VulkanContext.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <stdexcept>

// ---------------------------------------------------------------------------
bool VulkanContext::Init(int width, int height, const char* title) {
    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return false; }
    if (!glfwVulkanSupported()) { fprintf(stderr, "Vulkan not supported by GLFW\n"); return false; }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) { fprintf(stderr, "glfwCreateWindow failed\n"); return false; }

    if (!createInstance(title)) return false;

    if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create window surface\n");
        return false;
    }

    if (!pickPhysicalDeviceAndQueue()) return false;
    if (!createLogicalDevice()) return false;
    if (!createSwapchain()) return false;
    if (!createRenderPass()) return false;
    if (!createFramebuffers()) return false;
    if (!createCommandPoolAndBuffers()) return false;
    if (!createSyncObjects()) return false;
    if (!createDescriptorPool()) return false;

    glfwGetFramebufferSize(window_, &fbWidth_, &fbHeight_);
    return true;
}

bool VulkanContext::createInstance(const char* title) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = title;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "TinyShaderEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3; // Vulkan 1.3.290.0 SDK target

    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
#ifdef _DEBUG
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    ci.enabledLayerCount = 1;
    ci.ppEnabledLayerNames = layers;
#endif

    return vkCreateInstance(&ci, nullptr, &instance_) == VK_SUCCESS;
}

bool VulkanContext::pickPhysicalDeviceAndQueue() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) { fprintf(stderr, "No Vulkan-capable GPU found\n"); return false; }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    for (auto dev : devices) {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, qProps.data());

        for (uint32_t i = 0; i < qCount; i++) {
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &presentSupport);
            if ((qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
                physicalDevice_ = dev;
                graphicsQueueFamily_ = i;
                return true;
            }
        }
    }
    fprintf(stderr, "No suitable graphics+present queue family found\n");
    return false;
}

bool VulkanContext::createLogicalDevice() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = graphicsQueueFamily_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    const char* deviceExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qci;
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = deviceExts;
    ci.pEnabledFeatures = &features;

    if (vkCreateDevice(physicalDevice_, &ci, nullptr, &device_) != VK_SUCCESS) return false;
    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    return true;
}

bool VulkanContext::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());

    VkSurfaceFormatKHR chosen = formats[0];
    for (auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) chosen = f;

    int w, h; glfwGetFramebufferSize(window_, &w, &h);
    VkExtent2D extent = caps.currentExtent.width != UINT32_MAX
        ? caps.currentExtent
        : VkExtent2D{ std::clamp((uint32_t)w, caps.minImageExtent.width, caps.maxImageExtent.width),
                      std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height) };

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surface_;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // vsync; swap for MAILBOX if you want uncapped realtime preview
    ci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_) != VK_SUCCESS) return false;

    swapchainFormat_ = chosen.format;
    swapchainExtent_ = extent;

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &actualCount, nullptr);
    swapchainImages_.resize(actualCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &actualCount, swapchainImages_.data());

    swapchainImageViews_.resize(actualCount);
    for (uint32_t i = 0; i < actualCount; i++) {
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = swapchainImages_[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = swapchainFormat_;
        vci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(device_, &vci, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS) return false;
    }
    return true;
}

void VulkanContext::destroySwapchain() {
    for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    framebuffers_.clear();
    for (auto view : swapchainImageViews_) vkDestroyImageView(device_, view, nullptr);
    swapchainImageViews_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

bool VulkanContext::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &colorAttachment;
    ci.subpassCount = 1;
    ci.pSubpasses = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;

    return vkCreateRenderPass(device_, &ci, nullptr, &renderPass_) == VK_SUCCESS;
}

bool VulkanContext::createFramebuffers() {
    framebuffers_.resize(swapchainImageViews_.size());
    for (size_t i = 0; i < swapchainImageViews_.size(); i++) {
        VkImageView attachments[] = { swapchainImageViews_[i] };
        VkFramebufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass = renderPass_;
        ci.attachmentCount = 1;
        ci.pAttachments = attachments;
        ci.width = swapchainExtent_.width;
        ci.height = swapchainExtent_.height;
        ci.layers = 1;
        if (vkCreateFramebuffer(device_, &ci, nullptr, &framebuffers_[i]) != VK_SUCCESS) return false;
    }
    return true;
}

bool VulkanContext::createCommandPoolAndBuffers() {
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = graphicsQueueFamily_;
    if (vkCreateCommandPool(device_, &pci, nullptr, &commandPool_) != VK_SUCCESS) return false;

    commandBuffers_.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = commandPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = kMaxFramesInFlight;
    return vkAllocateCommandBuffers(device_, &ai, commandBuffers_.data()) == VK_SUCCESS;
}

bool VulkanContext::createSyncObjects() {
    VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < kMaxFramesInFlight; i++) {
        if (vkCreateSemaphore(device_, &sci, nullptr, &imageAvailable_[i]) != VK_SUCCESS) return false;
        if (vkCreateSemaphore(device_, &sci, nullptr, &renderFinished_[i]) != VK_SUCCESS) return false;
        if (vkCreateFence(device_, &fci, nullptr, &inFlightFence_[i]) != VK_SUCCESS) return false;
    }
    return true;
}

bool VulkanContext::createDescriptorPool() {
    // Generous pool sized for ImGui (fonts + any dynamic textures the
    // model/texture viewer wants to show later).
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64 },
    };
    VkDescriptorPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ci.maxSets = 128;
    ci.poolSizeCount = 2;
    ci.pPoolSizes = poolSizes;
    return vkCreateDescriptorPool(device_, &ci, nullptr, &descriptorPool_) == VK_SUCCESS;
}

// ---------------------------------------------------------------------------
bool VulkanContext::InitImGuiBackend() {
    ImGui_ImplGlfw_InitForVulkan(window_, true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = instance_;
    initInfo.PhysicalDevice = physicalDevice_;
    initInfo.Device = device_;
    initInfo.QueueFamily = graphicsQueueFamily_;
    initInfo.Queue = graphicsQueue_;
    initInfo.DescriptorPool = descriptorPool_;
    initInfo.RenderPass = renderPass_;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = static_cast<uint32_t>(swapchainImages_.size());
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&initInfo)) return false;
    imguiBackendInitialized_ = true;
    return true;
}

void VulkanContext::ShutdownImGuiBackend() {
    if (!imguiBackendInitialized_) return;
    vkDeviceWaitIdle(device_);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    imguiBackendInitialized_ = false;
}

// ---------------------------------------------------------------------------
bool VulkanContext::AcquireAndBeginRecording() {
    vkWaitForFences(device_, 1, &inFlightFence_[currentFrame_], VK_TRUE, UINT64_MAX);

    VkResult acquireResult = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
        imageAvailable_[currentFrame_], VK_NULL_HANDLE, &currentImageIndex_);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return false; }

    vkResetFences(device_, 1, &inFlightFence_[currentFrame_]);

    VkCommandBuffer cmd = commandBuffers_[currentFrame_];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &bi);
    // Command buffer is now open for extra passes (e.g. ShadowMap) to record
    // into before the swapchain render pass begins.
    return true;
}

void VulkanContext::BeginSwapchainRenderPass() {
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    VkClearValue clearColor{};
    clearColor.color = { {0.09f, 0.09f, 0.11f, 1.0f} }; // IDE-style dark background

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = renderPass_;
    rpBegin.framebuffer = framebuffers_[currentImageIndex_];
    rpBegin.renderArea.extent = swapchainExtent_;
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanContext::EndFrame() {
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];

    // ImGui draw data is recorded by the caller (Application) before EndFrame.
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkSemaphore waitSemaphores[] = { imageAvailable_[currentFrame_] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { renderFinished_[currentFrame_] };

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = waitSemaphores;
    submit.pWaitDstStageMask = waitStages;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(graphicsQueue_, 1, &submit, inFlightFence_[currentFrame_]);

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = signalSemaphores;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &currentImageIndex_;

    VkResult presentResult = vkQueuePresentKHR(graphicsQueue_, &present);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
}

void VulkanContext::recreateSwapchain() {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    while (w == 0 || h == 0) { glfwGetFramebufferSize(window_, &w, &h); glfwWaitEvents(); }

    vkDeviceWaitIdle(device_);
    destroySwapchain();
    createSwapchain();
    createFramebuffers();
    fbWidth_ = w; fbHeight_ = h;
}

bool VulkanContext::ShouldClose() const {
    return glfwWindowShouldClose(window_);
}

// ---------------------------------------------------------------------------
void VulkanContext::Shutdown() {
    if (device_) vkDeviceWaitIdle(device_);

    for (int i = 0; i < kMaxFramesInFlight; i++) {
        if (imageAvailable_[i]) vkDestroySemaphore(device_, imageAvailable_[i], nullptr);
        if (renderFinished_[i]) vkDestroySemaphore(device_, renderFinished_[i], nullptr);
        if (inFlightFence_[i]) vkDestroyFence(device_, inFlightFence_[i], nullptr);
    }
    if (commandPool_) vkDestroyCommandPool(device_, commandPool_, nullptr);
    destroySwapchain();
    if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
    if (descriptorPool_) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    if (device_) vkDestroyDevice(device_, nullptr);
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}
