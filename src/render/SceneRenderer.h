#pragma once
#include "../scene/Scene.h"
#include "../core/Math.h"
#include "effects/ShadowMap.h"
#include <vulkan/vulkan.h>
#include <unordered_map>

// The piece every deferred effect was waiting on: an actual GPU-shaded
// render of the scene (Lambertian lighting + real ShadowMap sampling),
// producing:
//   - a color image  (GetColorView)       -> Bloom / GodRays / DoF input
//   - a linear depth image (GetLinearDepthView) -> DoF / GodRays occlusion
// Both are sized once at Init and reused every frame.
class SceneRenderer {
public:
    bool Init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool pool, VkQueue queue,
              uint32_t width, uint32_t height, VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT);
    void Shutdown(VkDevice device);

    // Records the shading pass into cmd. shadowMap supplies its depth view
    // + comparison sampler (and whether it's enabled) for real shadow sampling.
    void RecordPass(VkCommandBuffer cmd, const Scene& scene, const ShadowMap& shadowMap, float aspect);

    // Projects the light's world position into this pass's 0..1 UV space —
    // used by GodRays to know where to radiate from.
    struct UV { float x, y; bool valid; };
    UV BuildLightScreenUV(const Scene& scene, float aspect) const;

    VkImageView GetColorView() const { return colorView_; }
    VkImageView GetLinearDepthView() const { return linearDepthView_; }
    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }
    bool IsAvailable() const { return pipeline_ != VK_NULL_HANDLE && framebuffer_ != VK_NULL_HANDLE && uboMapped_[0] != nullptr; }
    void ClearTextureCache() { textureSets_.clear(); }


private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    uint32_t width_ = 0, height_ = 0;
    VkFormat colorFormat_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;

    VkImage colorImage_ = VK_NULL_HANDLE; VkDeviceMemory colorMemory_ = VK_NULL_HANDLE; VkImageView colorView_ = VK_NULL_HANDLE;
    VkImage linearDepthImage_ = VK_NULL_HANDLE; VkDeviceMemory linearDepthMemory_ = VK_NULL_HANDLE; VkImageView linearDepthView_ = VK_NULL_HANDLE;
    VkImage depthImage_ = VK_NULL_HANDLE; VkDeviceMemory depthMemory_ = VK_NULL_HANDLE; VkImageView depthView_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout textureSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkShaderModule vertShader_ = VK_NULL_HANDLE, fragShader_ = VK_NULL_HANDLE;

    // Double-buffered UBO + descriptor set (see Bloom.cpp for why: avoids
    // updating GPU-visible state that a previous frame's in-flight work
    // might still be reading).
    static constexpr int kFramesInFlight = 2;
    VkBuffer ubo_[kFramesInFlight] = {}; VkDeviceMemory uboMemory_[kFramesInFlight] = {}; void* uboMapped_[kFramesInFlight] = {};
    VkDescriptorSet descSets_[kFramesInFlight] = {};
    std::unordered_map<const MaterialTexture*, VkDescriptorSet> textureSets_;
    VkImage whiteTextureImage_ = VK_NULL_HANDLE; VkDeviceMemory whiteTextureMemory_ = VK_NULL_HANDLE;
    VkImageView whiteTextureView_ = VK_NULL_HANDLE; VkSampler whiteTextureSampler_ = VK_NULL_HANDLE;
    int frameSlot_ = 0;
    VkDescriptorSet textureSetFor(const MaterialTexture* texture);
    bool createWhiteTexture(VkCommandPool pool, VkQueue queue);
};
