#pragma once
#include "VulkanContext.h"
#include "../scene/Scene.h"
#include "../render/effects/EffectStubs.h"
#include "../render/SceneRenderer.h"
#include "../render/PostComposite.h"
#include "../ui/UIManager.h"
#include <memory>

class Application {
public:
    bool Init();
    void Run();
    void Shutdown();

private:
    VulkanContext vulkan_;
    Scene scene_;
    EffectStack effects_;
    SceneRenderer sceneRenderer_;   // real GPU color+linearDepth pass, samples ShadowMap
    PostComposite composite_;       // final combine: (DoF+ColorCorrect base) + Bloom + GodRays
    VkDescriptorSet compositeImGuiTexture_ = VK_NULL_HANDLE; // registered once via ImGui_ImplVulkan_AddTexture

    // Constructed in Init() — needs the Vulkan device, which doesn't exist yet
    // at Application's own construction time.
    std::unique_ptr<UIManager> ui_;

    bool realtimePreview_ = true; // Feature 4: 'K' toggles this
    float fps_ = 0.0f;

    void handleGlobalInput();
    void updateFps(float dt);
    void renderPipeline(VkCommandBuffer cmd); // Shadows -> Scene -> Bloom/DoF -> ColorCorrect -> GodRays -> Composite
};
