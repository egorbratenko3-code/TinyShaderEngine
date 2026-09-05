#pragma once
#include <vulkan/vulkan.h>
#include <string>

// Real god rays: screen-space radial blur from the light's projected screen
// position, using SceneRenderer's linear depth output to occlude rays where
// real geometry sits in front of the light. Output is additive — Composite
// adds it directly onto the final image.
class GodRays {
public:
    bool enabled = false;
    float density = 0.5f;
    float decay = 0.95f;
    float weight = 0.3f;
    int samples = 64; // clamped to [1, kMaxSamples] (shader has a matching compile-time bound)

    static constexpr int kMaxSamples = 128;

    bool Init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool pool, VkQueue queue,
              uint32_t width, uint32_t height, VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT);
    void Shutdown(VkDevice device);

    // depthView: SceneRenderer's linear-depth view. lightScreenUV/valid:
    // from SceneRenderer::BuildLightScreenUV. lightColor: scene.light.colorTint.
    void RecordPass(VkCommandBuffer cmd, VkImageView depthView, float lightUvX, float lightUvY, bool lightVisible,
                     float lightColorR, float lightColorG, float lightColorB);

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
