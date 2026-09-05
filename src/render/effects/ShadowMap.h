#pragma once
#include "../../core/Math.h"
#include "../../scene/Scene.h"
#include <vulkan/vulkan.h>
#include <string>

// Real Vulkan implementation of the ShadowMap deferred feature.
//
// What this does: renders scene geometry into a depth-only image from the
// light's point of view (a standard shadow map), producing a real
// VkImageView + VkSampler that a future shading pass can sample for
// occlusion. Init()/Resize() build the Vulkan-side resources; RenderPass()
// records the actual depth draw into the frame's command buffer, called
// from Application before the swapchain render pass begins.
//
// Scope note: since the main viewport doesn't have a GPU shading pass yet
// (see project README), nothing currently *samples* this shadow map to
// darken pixels — but the depth texture it produces is real and correct,
// and IsAvailable()/GetDepthView()/GetSampler() are ready for that pass
// to consume when it exists.
class ShadowMap {
public:
    bool enabled = false;
    int  resolution = 2048;
    float bias = 0.005f;
    float pcfRadius = 1.0f; // reserved for the sampling shader (unused by the depth pass itself)

    bool Init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool pool, VkQueue queue);
    void Shutdown(VkDevice device);

    // Rebuilds the depth image/framebuffer at the current `resolution`.
    // Call after changing `resolution` from the UI, and once after Init().
    bool Resize(VkDevice device, VkPhysicalDevice physicalDevice);

    // Records the depth-only pass into `cmd` for every uploaded model in
    // `scene`. Models that haven't had Model::UploadToGPU() called yet are
    // skipped (Application uploads on load — see Application::onModelLoaded).
    void RenderPass(VkCommandBuffer cmd, const Scene& scene);

    Mat4 BuildLightViewProj(const Scene& scene) const;

    bool IsAvailable() const { return depthView_ != VK_NULL_HANDLE; }
    VkImageView GetDepthView() const { return depthView_; }
    VkSampler GetSampler() const { return sampler_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;

    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkShaderModule vertShader_ = VK_NULL_HANDLE;

    int builtResolution_ = 0;

    bool createStaticResources();     // render pass, pipeline layout, pipeline, sampler (once)
    bool createResolutionResources(); // depth image/view/framebuffer (on Init + Resize)
    void destroyResolutionResources();
    VkShaderModule loadShaderModule(const std::string& spvPath);
};
