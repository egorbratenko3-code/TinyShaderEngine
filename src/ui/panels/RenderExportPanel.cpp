#include "RenderExportPanel.h"
#include "imgui.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

// stb_image_write is header-only: exactly one translation unit in the whole
// project must define STB_IMAGE_WRITE_IMPLEMENTATION before including it,
// or stbi_write_png() has no body and the linker fails. This is that TU.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace {
    void setPixel(std::vector<uint8_t>& fb, int w, int h, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        size_t i = (size_t)(y * w + x) * 4;
        fb[i+0] = r; fb[i+1] = g; fb[i+2] = b; fb[i+3] = a;
    }
    void drawLine(std::vector<uint8_t>& fb, int w, int h, int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (true) {
            setPixel(fb, w, h, x0, y0, r, g, b);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}


std::string RenderExportPanel::promptSavePath() {
    char filePath[MAX_PATH] = "render_output.png";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "PNG Image (*.png)\0*.png\0All Files\0*.*\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "png";
    ofn.lpstrTitle = "Export HQ Render";
    if (GetSaveFileNameA(&ofn)) return std::string(filePath);
    return "";
}

void RenderExportPanel::startRender() {
    targetWidth_  = useCustomRes_ ? customWidth_  : kResolutions[resIndex_][0];
    targetHeight_ = useCustomRes_ ? customHeight_ : kResolutions[resIndex_][1];
    targetWidth_  = std::clamp(targetWidth_, 16, 8192);
    targetHeight_ = std::clamp(targetHeight_, 16, 8192);

    frameBuffer_.assign((size_t)targetWidth_ * targetHeight_ * 4, 0);
    // background fill
    for (size_t i = 0; i < frameBuffer_.size(); i += 4) {
        frameBuffer_[i+0] = 18; frameBuffer_[i+1] = 18; frameBuffer_[i+2] = 22; frameBuffer_[i+3] = 255;
    }

    renderedScanlines_ = 0;
    isRendering_ = true;
    statusMessage_ = "Rendering...";
}

void RenderExportPanel::stepRender(int scanlinesPerFrame) {
    if (!isRendering_) return;

    // Geometry pass happens once (cheap enough at these vertex counts); the
    // "progress" here simulates a tile/scanline-based renderer so the bar
    // has something real to track. Swap this for actual per-tile GPU/path
    // -trace work when the shading pipeline exists.
    if (renderedScanlines_ == 0) {
        float aspect = (float)targetWidth_ / (float)targetHeight_;
        const auto& cam = scene_.ActiveCamera();
        Mat4 view = Mat4::lookAt(cam.position, cam.target, Vec3{0,1,0});
        float fovRad = cam.fovDegrees * 3.14159265f / 180.0f;
        Mat4 proj = Mat4::perspective(fovRad, aspect, 0.05f, 500.0f);

        Mat4 vp = Mat4::mul(proj, view);

        auto project = [&](const Vec3& p, int& sx, int& sy, const Mat4& m) -> bool {
            float clip[4]; m.transformPoint(p, clip);
            if (clip[3] <= 0.0001f) return false;
            float ndcX = clip[0] / clip[3], ndcY = clip[1] / clip[3];
            sx = (int)((ndcX * 0.5f + 0.5f) * targetWidth_);
            sy = (int)((1.0f - (ndcY * 0.5f + 0.5f)) * targetHeight_);
            return true;
        };

        // model wireframes (per-object transform: move/rotate, same as ViewportPanel)
        for (auto& model : scene_.GetModels()) {
            Mat4 mvpModel = Mat4::mul(vp, model->GetModelMatrix());
            const auto& verts = model->GetVertices();
            for (auto& sub : model->GetSubMeshes()) {
                for (size_t i = 0; i + 2 < sub.indices.size(); i += 3) {
                    int x0=0,y0=0,x1=0,y1=0,x2=0,y2=0; // zero-init: project() can return false without setting sx/sy
                    bool ok0 = project(verts[sub.indices[i+0]].position, x0, y0, mvpModel);
                    bool ok1 = project(verts[sub.indices[i+1]].position, x1, y1, mvpModel);
                    bool ok2 = project(verts[sub.indices[i+2]].position, x2, y2, mvpModel);
                    if (ok0 && ok1) drawLine(frameBuffer_, targetWidth_, targetHeight_, x0,y0,x1,y1, 160,200,255);
                    if (ok1 && ok2) drawLine(frameBuffer_, targetWidth_, targetHeight_, x1,y1,x2,y2, 160,200,255);
                    if (ok2 && ok0) drawLine(frameBuffer_, targetWidth_, targetHeight_, x2,y2,x0,y0, 160,200,255);
                }
            }
        }

    }

    renderedScanlines_ = std::min(renderedScanlines_ + scanlinesPerFrame, targetHeight_);
    if (renderedScanlines_ >= targetHeight_) {
        finishRenderAndExport();
    }
}

void RenderExportPanel::finishRenderAndExport() {
    isRendering_ = false;
    outputPath_ = promptSavePath();
    if (outputPath_.empty()) {
        statusMessage_ = "Export cancelled.";
        return;
    }
    int ok = stbi_write_png(outputPath_.c_str(), targetWidth_, targetHeight_, 4,
                             frameBuffer_.data(), targetWidth_ * 4);
    statusMessage_ = ok ? ("Exported: " + outputPath_) : "Failed to write PNG.";
}

void RenderExportPanel::Draw() {
    ImGui::Begin("HQ Render & Export");

    ImGui::Checkbox("Custom resolution", &useCustomRes_);
    if (useCustomRes_) {
        ImGui::InputInt("Width", &customWidth_);
        ImGui::InputInt("Height", &customHeight_);
    } else {
        const char* labels[kResCount] = { "1280 x 720", "1920 x 1080 (FHD)", "2560 x 1440 (QHD)", "3840 x 2160 (4K)", "512 x 512 (square)" };
        ImGui::Combo("Resolution", &resIndex_, labels, kResCount);
    }

    ImGui::Separator();

    if (!isRendering_) {
        if (ImGui::Button("Render & Export PNG...", ImVec2(-1, 0))) startRender();
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("Rendering...", ImVec2(-1, 0));
        ImGui::EndDisabled();
    }

    if (isRendering_) {
        stepRender();
        float progress = targetHeight_ > 0 ? (float)renderedScanlines_ / (float)targetHeight_ : 0.0f;
        ImGui::ProgressBar(progress, ImVec2(-1, 0));
        ImGui::Text("%d x %d", targetWidth_, targetHeight_);
    }

    if (!statusMessage_.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", statusMessage_.c_str());
    }

    ImGui::End();
}
