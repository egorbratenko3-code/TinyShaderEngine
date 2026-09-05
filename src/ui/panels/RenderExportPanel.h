#pragma once
#include "../../scene/Scene.h"
#include "../../core/Math.h"
#include <vector>
#include <cstdint>
#include <string>

// Feature 5: resolution picker + progress bar + PNG export.
//
// Renders the same grid/node/wireframe representation the ViewportPanel
// shows (see class comment there) into an off-screen CPU pixel buffer at
// the chosen resolution, then writes it out with stb_image_write. This
// keeps preview and export visually consistent today; swap `renderTile()`
// for a real Vulkan offscreen pass once GPU shading is implemented.
class RenderExportPanel {
public:
    explicit RenderExportPanel(Scene& scene) : scene_(scene) {}
    void Draw();

private:
    Scene& scene_;

    int resIndex_ = 1;
    static constexpr int kResCount = 5;
    const int kResolutions[5][2] = { {1280,720}, {1920,1080}, {2560,1440}, {3840,2160}, {512,512} };
    int customWidth_ = 1920, customHeight_ = 1080;
    bool useCustomRes_ = false;

    bool isRendering_ = false;
    int renderedScanlines_ = 0;
    int targetWidth_ = 0, targetHeight_ = 0;
    std::vector<uint8_t> frameBuffer_; // RGBA8
    std::string outputPath_;
    std::string statusMessage_;

    void startRender();
    void stepRender(int scanlinesPerFrame = 24); // advances progress; call every Draw()
    void finishRenderAndExport();
    std::string promptSavePath();
};
