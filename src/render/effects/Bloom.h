#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

// Real bloom: bright-pass prefilter -> box-filter downsample chain ->
// tent-filter upsample chain, additively combined (the standard
// "dual filtering" bloom used in COD/Unity/Unreal). Self-contained module —
// give it an input color view each frame via RecordPass().
//
// Scope note: there is currently no scene color render producing that input
// view (the viewport is still CPU-drawn, see project README / ShadowMap's
// notes), so this has nothing real to bloom yet. The pipeline, shaders, and
// mip chain below are fully functional Vulkan — plug in a real scene color
// image (e.g. the eventual main shading pass's output) and it will work.
class Bloom {
public:
    bool enabled = false;
    float threshold = 1.0f;
    float intensity = 0.5f;   // drives upsample tent-filter radius
    int mipLevels = 5;        // clamped to [2, kMaxMips]

    static constexpr int kMaxMips = 8;

    bool Init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool pool, VkQueue queue,
              uint32_t baseWidth, uint32_t baseHeight, VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT);
    void Shutdown(VkDevice device);

    // inputColorView: any sampleable view at (or above) baseWidth x baseHeight,
    // matching `colorFormat` passed to Init. Records the full chain into cmd.
    void RecordPass(VkCommandBuffer cmd, VkImageView inputColorView);

    // Bloom contribution at full (mip 0) resolution — additive; a future
    // scene-composite pass blends this onto the final color with `intensity`.
    VkImageView GetResultView() const { return mips_.empty() ? VK_NULL_HANDLE : mips_[0].view; }
    bool IsAvailable() const { return !mips_.empty(); }

private:
    struct MipTarget {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer fbClear = VK_NULL_HANDLE; // render pass variant: LOAD_OP_CLEAR (downsample target)
        VkFramebuffer fbLoad = VK_NULL_HANDLE;  // render pass variant: LOAD_OP_LOAD  (upsample additive target)
        VkDescriptorSet readSet = VK_NULL_HANDLE; // static: always samples this mip's own view
        uint32_t width = 0, height = 0;
    };

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkFormat colorFormat_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    uint32_t baseWidth_ = 0, baseHeight_ = 0;

    VkRenderPass renderPassClear_ = VK_NULL_HANDLE;
    VkRenderPass renderPassAdditive_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline downsamplePipeline_ = VK_NULL_HANDLE;
    VkPipeline upsamplePipeline_ = VK_NULL_HANDLE;
    VkShaderModule vertShader_ = VK_NULL_HANDLE, downsampleFrag_ = VK_NULL_HANDLE, upsampleFrag_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    std::vector<MipTarget> mips_;
    VkDescriptorSet externalInputSets_[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE }; // double-buffered: see .cpp note
    int frameSlot_ = 0;

    bool createStaticResources();
    bool createMipChain();
    void destroyMipChain();
    VkShaderModule loadShader(const std::string& path);
    void writeDescriptor(VkDescriptorSet set, VkImageView view);
};
