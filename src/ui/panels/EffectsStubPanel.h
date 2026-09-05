#pragma once
#include "../../render/effects/EffectStubs.h"
#include "../../render/PostComposite.h"
#include <vulkan/vulkan.h>

// ShadowMap now drives real Vulkan resources, so this panel needs the
// device/physicalDevice to call ShadowMap::Resize() when the resolution
// slider changes.
class EffectsStubPanel {
public:
    EffectsStubPanel(EffectStack& effects, PostComposite& composite, VkDevice device, VkPhysicalDevice physicalDevice)
        : effects_(effects), composite_(composite), device_(device), physicalDevice_(physicalDevice) {}
    void Draw();

private:
    EffectStack& effects_;
    PostComposite& composite_;

    VkDevice device_;
    VkPhysicalDevice physicalDevice_;
    void drawStubHeader(const char* name, bool& enabled, bool implemented);
};
