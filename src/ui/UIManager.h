#pragma once
#include "../scene/Scene.h"
#include "../render/effects/EffectStubs.h"
#include "../render/PostComposite.h"
#include "panels/ViewportPanel.h"
#include "panels/ModelLoaderPanel.h"
#include "panels/RenderExportPanel.h"
#include "panels/EffectsStubPanel.h"
#include "panels/ScenePanel.h"
#include <vulkan/vulkan.h>

// Builds the IDE-style dockspace layout and draws every panel each frame.
// Layout (first run):
//   +--------------------------------------------------+
//   | Menu Bar                                          |
//   +---------------+------------------------+-----------+
//   | Model Loader   |                        | Effects   |
//   | (left dock)    |      Viewport          +-----------+
//   +---------------+      (center dock)      |  Scene    |
//   | Render/Export  |                        | (camera/  |
//   | (bottom-left)  |                        |  light)   |
//   +---------------+------------------------+-----------+
class UIManager {
public:
    // device/physicalDevice are only needed by EffectsStubPanel now, to
    // rebuild the ShadowMap depth image when its resolution changes.
    UIManager(Scene& scene, EffectStack& effects, PostComposite& composite, VkDevice device, VkPhysicalDevice physicalDevice)
        : viewport_(scene), modelLoader_(scene), renderExport_(scene),
          effectsPanel_(effects, composite, device, physicalDevice), scenePanel_(scene) {}

    void SetUpDockspace();  // call once (or when layout is reset)
    void Draw(bool realtimePreview, float fps, ImTextureID sceneTexture = 0);

    ViewportPanel& Viewport() { return viewport_; }

private:
    ViewportPanel viewport_;
    ModelLoaderPanel modelLoader_;
    RenderExportPanel renderExport_;
    EffectsStubPanel effectsPanel_;
    ScenePanel scenePanel_;
    bool dockspaceBuilt_ = false;

    void drawMenuBar();
};
