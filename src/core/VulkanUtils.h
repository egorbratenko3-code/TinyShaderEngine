#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

// Small shared helpers for one-off Vulkan buffer work. Added for ShadowMap,
// which is the first effect that needs real GPU-side geometry (a depth-only
// pass has to rasterize actual vertex/index buffers, not the CPU wireframe
// the viewport uses).
namespace vkutil {

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);

void CreateBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size,
                   VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                   VkBuffer& outBuffer, VkDeviceMemory& outMemory);

VkCommandBuffer BeginSingleTimeCommands(VkDevice device, VkCommandPool pool);
void EndSingleTimeCommands(VkDevice device, VkCommandPool pool, VkQueue queue, VkCommandBuffer cmd);

// Uploads `data` into a DEVICE_LOCAL buffer with `usage`, via a temporary
// staging buffer. Caller owns the returned buffer/memory and must destroy them.
void UploadToDeviceLocalBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                                VkCommandPool pool, VkQueue queue,
                                const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                                VkBuffer& outBuffer, VkDeviceMemory& outMemory);

// Creates a color-attachment-capable, sampleable 2D image (used by Bloom's
// mip chain and DoF's output target). Caller destroys image/memory/view.
void CreateColorAttachment(VkDevice device, VkPhysicalDevice physicalDevice,
                            uint32_t width, uint32_t height, VkFormat format,
                            VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView);

// One-time clear-to-black + transition to SHADER_READ_ONLY_OPTIMAL, so an
// optional effect's output (e.g. Bloom/GodRays when toggled off) is always
// safely sampleable by a downstream composite pass even before it has ever
// recorded a real pass.
void ClearColorToBlackAndTransition(VkDevice device, VkCommandPool pool, VkQueue queue, VkImage image);

} // namespace vkutil
