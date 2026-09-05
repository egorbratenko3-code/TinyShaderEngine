#pragma once
#include <vulkan/vulkan.h>
#include <string>

// Real color grading: single fullscreen pass applying color filter,
// exposure, contrast, and saturation. Fully wired into the pipeline —
// Application feeds it whatever the DoF stage (or raw scene color, if DoF
// is off) produced, and its output feeds the final Composite pass.
class ColorCorrect {
public:
    bool enabled = false;
    float exposure = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float colorFilter[3] = {1.0f, 1.0f, 1.0f};

    bool Init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool pool, VkQueue queue,
              uint32_t width, uint32_t height, VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT);
    void Shutdown(VkDevice device);
    void RecordPass(VkCommandBuffer cmd, VkImageView inputColorView);

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

    VkDescriptorSet inputSets_[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    int frameSlot_ = 0;

    VkShaderModule loadShader(const std::string& path);
};
