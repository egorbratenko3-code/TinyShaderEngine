#pragma once
#include <vulkan/vulkan.h>
#include <string>

// Real depth-of-field: single fullscreen pass that computes a circle of
// confusion from a linear depth texture and does a radius-scaled radial
// blur of the scene color. Self-contained — give it color + depth views
// each frame via RecordPass().
//
// Scope note: same as Bloom — there's no scene color OR depth render yet
// (no main shading pass, and the only depth this engine produces is
// ShadowMap's light-space depth, which is the wrong space for DoF, which
// needs camera-space depth). The pipeline/shader below are fully
// functional; wire in real camera-space depth once a shading pass exists.
class DepthOfField {
public:
    bool enabled = false;
    float focusDistance = 4.0f;
    float focusRange = 2.0f;
    float bokehStrength = 1.0f;

    bool Init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool pool, VkQueue queue,
              uint32_t width, uint32_t height, VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT);
    void Shutdown(VkDevice device);

    // colorView/depthView: sampleable views at Init's resolution. depthView
    // is expected to hold linear view-space depth in its .r channel.
    void RecordPass(VkCommandBuffer cmd, VkImageView colorView, VkImageView depthView);

    VkImageView GetResultView() const { return outputView_; }
    bool IsAvailable() const { return outputView_ != VK_NULL_HANDLE; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkFormat colorFormat_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    uint32_t width_ = 0, height_ = 0;

    VkImage outputImage_ = VK_NULL_HANDLE;
    VkDeviceMemory outputMemory_ = VK_NULL_HANDLE;
    VkImageView outputView_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkShaderModule vertShader_ = VK_NULL_HANDLE, fragShader_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkDescriptorSet inputSets_[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE }; // double-buffered, see Bloom.cpp note
    int frameSlot_ = 0;

    VkShaderModule loadShader(const std::string& path);
};
