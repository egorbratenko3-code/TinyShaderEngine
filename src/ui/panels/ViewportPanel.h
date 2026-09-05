#pragma once
#include "../../scene/Scene.h"
#include "../../core/Math.h"
#include "imgui.h"

// Feature 2 (empty-scene grid), Feature 3 (draggable Camera/Light nodes),
// Feature 4 (realtime toggle + FPS counter) all live here, plus free camera
// flight (WASD move + arrow-key look) and per-model transform dragging
// (green "M" marker).
//
// Rendering: sceneTexture (when valid) is the fully GPU-shaded, post-
// processed result — SceneRenderer -> Bloom/DoF -> ColorCorrect -> GodRays
// -> Composite (see Application::renderPipeline) — drawn as the viewport's
// background via ImGui::Image. The grid, camera/light/model node markers,
// and model wireframe overlay are still drawn on top via ImGui's draw list
// using the CPU-side projection below; they're UI gizmos, not scene
// geometry, so they stay this way even with real GPU shading underneath.
enum class GizmoAxis {
    None,
    X,
    Y,
    Z,
    Center
};

class ViewportPanel {
public:
    explicit ViewportPanel(Scene& scene) : scene_(scene) {}

    // realtimePreview: when false, viewport freezes on the last frame
    // (still draws once) instead of recomputing every frame — Feature 4.
    // sceneTexture: ImGui texture handle for the composited GPU render
    // (VK_NULL_HANDLE-equivalent/0 until Application has registered one).
    void Draw(bool realtimePreview, float fps, ImTextureID sceneTexture = 0);

    // Exposed so RenderExportPanel can reuse the exact same projection
    // for HQ render/export (Feature 5), keeping preview and export consistent.
    Mat4 BuildViewMatrix() const;
    Mat4 BuildProjMatrix(float aspect) const;

private:
    Scene& scene_;
    ImVec2 viewportSize_{800, 600};
    ImVec2 viewportOrigin_{0, 0};

    SceneNode* draggedNode_ = nullptr;
    SceneNode* contextNode_ = nullptr;

    // 3D Translation Gizmo state
    GizmoAxis activeGizmoAxis_ = GizmoAxis::None;
    GizmoAxis hoveredGizmoAxis_ = GizmoAxis::None;
    SceneNode* gizmoTargetNode_ = nullptr;
    Vec3 gizmoDragStartPos_{0, 0, 0};

    bool projectToScreen(const Vec3& worldPos, const Mat4& vp, ImVec2& outScreen) const;
    void drawGrid(ImDrawList* dl, const Mat4& vp, float sizeWorld = 10.0f, int divisions = 20);
    void drawNode(ImDrawList* dl, SceneNode& node, const Mat4& vp);
    void drawCameraDirection(ImDrawList* dl, const CameraNode& cam, const Mat4& vp);
    void drawLightDirection(ImDrawList* dl, const LightNode& light, const Mat4& vp);
    void drawGizmo(ImDrawList* dl, const Mat4& vp);
    void handleGizmoInteraction(const Mat4& vp);
    void drawTransformOverlay();
    void drawModelsWireframe(ImDrawList* dl, const Mat4& vp);
    void handleNodeDragging(const Mat4& vp);
    void handleCameraFlight(); // WASD move + arrow-key look, active while the viewport window is focused
};

