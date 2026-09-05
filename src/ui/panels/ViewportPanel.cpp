#include "ViewportPanel.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace {
static float DistToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    ImVec2 ab = { b.x - a.x, b.y - a.y };
    float lenSq = ab.x * ab.x + ab.y * ab.y;
    if (lenSq < 1e-4f) {
        float dx = p.x - a.x, dy = p.y - a.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    float t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq, 0.0f, 1.0f);
    ImVec2 proj = { a.x + t * ab.x, a.y + t * ab.y };
    float dx = p.x - proj.x, dy = p.y - proj.y;
    return std::sqrt(dx * dx + dy * dy);
}
}

Mat4 ViewportPanel::BuildViewMatrix() const {
    const auto& cam = scene_.ActiveCamera();
    return Mat4::lookAt(cam.position, cam.target, Vec3{0, 1, 0});
}

Mat4 ViewportPanel::BuildProjMatrix(float aspect) const {
    const auto& cam = scene_.ActiveCamera();
    float fovRad = cam.fovDegrees * 3.14159265f / 180.0f;
    return Mat4::perspective(fovRad, aspect, 0.05f, 500.0f);
}

bool ViewportPanel::projectToScreen(const Vec3& worldPos, const Mat4& vp, ImVec2& outScreen) const {
    float clip[4];
    vp.transformPoint(worldPos, clip);
    if (clip[3] <= 0.0001f) return false; // behind camera
    float ndcX = clip[0] / clip[3];
    float ndcY = clip[1] / clip[3];
    outScreen.x = viewportOrigin_.x + (ndcX * 0.5f + 0.5f) * viewportSize_.x;
    outScreen.y = viewportOrigin_.y + (1.0f - (ndcY * 0.5f + 0.5f)) * viewportSize_.y;
    return true;
}

void ViewportPanel::drawGrid(ImDrawList* dl, const Mat4& vp, float sizeWorld, int divisions) {
    const ImU32 lineColor  = IM_COL32(90, 90, 100, 140);
    const ImU32 axisXColor = IM_COL32(210, 70, 70, 200);
    const ImU32 axisZColor = IM_COL32(70, 120, 210, 200);

    float half = sizeWorld * 0.5f;
    float step = sizeWorld / divisions;

    for (int i = 0; i <= divisions; i++) {
        float t = -half + i * step;
        ImVec2 a, b;
        if (projectToScreen({t, 0, -half}, vp, a) && projectToScreen({t, 0, half}, vp, b))
            dl->AddLine(a, b, std::fabs(t) < 1e-4f ? axisZColor : lineColor, std::fabs(t) < 1e-4f ? 2.0f : 1.0f);
        if (projectToScreen({-half, 0, t}, vp, a) && projectToScreen({half, 0, t}, vp, b))
            dl->AddLine(a, b, std::fabs(t) < 1e-4f ? axisXColor : lineColor, std::fabs(t) < 1e-4f ? 2.0f : 1.0f);
    }
}

void ViewportPanel::drawNode(ImDrawList* dl, SceneNode& node, const Mat4& vp) {
    ImVec2 screen;
    if (!projectToScreen(node.position, vp, screen)) return;

    ImU32 fill = IM_COL32((int)(node.colorRGB.x * 255), (int)(node.colorRGB.y * 255), (int)(node.colorRGB.z * 255), 255);
    float radius = node.isDragging ? 10.0f : 8.0f;

    dl->AddCircleFilled(screen, radius, fill);
    dl->AddCircle(screen, radius + 2.0f, IM_COL32(255, 255, 255, 180), 0, 1.5f);
    if (scene_.SelectedNode() == &node)
        dl->AddCircle(screen, radius + 5.0f, IM_COL32(255, 215, 80, 255), 0, 2.5f);

    ImVec2 textSize = ImGui::CalcTextSize(node.label.c_str());
    dl->AddText(ImVec2(screen.x - textSize.x * 0.5f, screen.y - radius - textSize.y - 3.0f),
                IM_COL32(255, 255, 255, 230), node.label.c_str());
}

void ViewportPanel::drawCameraDirection(ImDrawList* dl, const CameraNode& cam, const Mat4& vp) {
    ImVec2 from, to;
    if (!projectToScreen(cam.position, vp, from) ||
        !projectToScreen(cam.position + cam.Forward() * 0.8f, vp, to)) return;

    const ImU32 color = IM_COL32(95, 205, 255, 255);
    dl->AddLine(from, to, color, 2.5f);
    const ImVec2 d(to.x - from.x, to.y - from.y);
    const float len = std::sqrt(d.x*d.x + d.y*d.y);
    if (len < 1.0f) return;
    const ImVec2 n(d.x / len, d.y / len), side(-n.y, n.x);
    dl->AddTriangleFilled(to, ImVec2(to.x - n.x*10 + side.x*5, to.y - n.y*10 + side.y*5),
                           ImVec2(to.x - n.x*10 - side.x*5, to.y - n.y*10 - side.y*5), color);
}

void ViewportPanel::drawLightDirection(ImDrawList* dl, const LightNode& light, const Mat4& vp) {
    if (!light.isSpot) return;
    ImVec2 from, to;
    if (!projectToScreen(light.position, vp, from) ||
        !projectToScreen(light.position + light.AimDirection() * 1.0f, vp, to)) return;

    const ImU32 color = IM_COL32(255, 190, 70, 220);
    dl->AddLine(from, to, color, 2.0f);
    const ImVec2 d(to.x - from.x, to.y - from.y);
    const float len = std::sqrt(d.x*d.x + d.y*d.y);
    if (len < 1.0f) return;
    const ImVec2 n(d.x / len, d.y / len), side(-n.y, n.x);
    dl->AddTriangleFilled(to, ImVec2(to.x - n.x*9 + side.x*4.5f, to.y - n.y*9 + side.y*4.5f),
                           ImVec2(to.x - n.x*9 - side.x*4.5f, to.y - n.y*9 - side.y*4.5f), color);
}

// =============================================================================
// 3D Translation Gizmo (Visible Arrows: Red X, Green Y, Blue Z)
// =============================================================================
void ViewportPanel::drawGizmo(ImDrawList* dl, const Mat4& vp) {
    SceneNode* node = scene_.SelectedNode();
    if (!node) return;

    ImVec2 screenOrigin;
    if (!projectToScreen(node->position, vp, screenOrigin)) return;

    const auto& cam = scene_.ActiveCamera();
    float dist = (node->position - cam.position).length();
    float arrowWorldLen = std::max(dist * 0.16f, 0.25f);

    struct AxisInfo {
        GizmoAxis axis;
        Vec3 dir;
        ImU32 baseColor;
        const char* name;
    };

    AxisInfo axes[3] = {
        { GizmoAxis::X, Vec3{1, 0, 0}, IM_COL32(235, 55, 55, 255), "X" },
        { GizmoAxis::Y, Vec3{0, 1, 0}, IM_COL32(55, 220, 55, 255), "Y" },
        { GizmoAxis::Z, Vec3{0, 0, 1}, IM_COL32(55, 130, 245, 255), "Z" }
    };

    // Center disc (allows free camera-plane dragging)
    bool isCenterActive = (activeGizmoAxis_ == GizmoAxis::Center);
    bool isCenterHovered = (activeGizmoAxis_ == GizmoAxis::None && hoveredGizmoAxis_ == GizmoAxis::Center);
    ImU32 centerCol = (isCenterActive || isCenterHovered) ? IM_COL32(255, 240, 60, 255) : IM_COL32(220, 220, 220, 200);
    dl->AddCircleFilled(screenOrigin, 5.0f, centerCol);
    dl->AddCircle(screenOrigin, 6.5f, IM_COL32(0, 0, 0, 180), 0, 1.5f);

    for (const auto& a : axes) {
        ImVec2 screenTip;
        if (!projectToScreen(node->position + a.dir * arrowWorldLen, vp, screenTip)) continue;

        ImVec2 sVec = { screenTip.x - screenOrigin.x, screenTip.y - screenOrigin.y };
        float sLen = std::sqrt(sVec.x * sVec.x + sVec.y * sVec.y);
        if (sLen < 2.0f) continue;

        ImVec2 u = { sVec.x / sLen, sVec.y / sLen };
        ImVec2 n = { -u.y, u.x };

        bool isActive = (activeGizmoAxis_ == a.axis);
        bool isHovered = (activeGizmoAxis_ == GizmoAxis::None && hoveredGizmoAxis_ == a.axis);
        ImU32 col = (isActive || isHovered) ? IM_COL32(255, 240, 60, 255) : a.baseColor;
        float lineThick = (isActive || isHovered) ? 4.5f : 3.0f;
        float coneLen = (isActive || isHovered) ? 14.0f : 12.0f;
        float coneWidth = (isActive || isHovered) ? 6.5f : 5.0f;

        // Draw shaft line
        ImVec2 shaftEnd = { screenTip.x - u.x * (coneLen * 0.4f), screenTip.y - u.y * (coneLen * 0.4f) };
        dl->AddLine(screenOrigin, shaftEnd, col, lineThick);

        // Draw cone arrowhead
        ImVec2 coneBase = { screenTip.x - u.x * coneLen, screenTip.y - u.y * coneLen };
        ImVec2 p0 = screenTip;
        ImVec2 p1 = { coneBase.x + n.x * coneWidth, coneBase.y + n.y * coneWidth };
        ImVec2 p2 = { coneBase.x - n.x * coneWidth, coneBase.y - n.y * coneWidth };
        dl->AddTriangleFilled(p0, p1, p2, col);

        // Draw axis text label
        ImVec2 labelPos = { screenTip.x + u.x * 12.0f - 4.0f, screenTip.y + u.y * 12.0f - 6.0f };
        dl->AddText(labelPos, col, a.name);
    }
}

void ViewportPanel::handleGizmoInteraction(const Mat4& vp) {
    ImGuiIO& io = ImGui::GetIO();
    SceneNode* sel = scene_.SelectedNode();

    // 1. Calculate hover state if not currently dragging
    if (activeGizmoAxis_ == GizmoAxis::None) {
        hoveredGizmoAxis_ = GizmoAxis::None;
        if (sel) {
            ImVec2 screenOrigin;
            if (projectToScreen(sel->position, vp, screenOrigin)) {
                float dx = io.MousePos.x - screenOrigin.x, dy = io.MousePos.y - screenOrigin.y;
                float dCenter = std::sqrt(dx * dx + dy * dy);
                if (dCenter <= 10.0f) {
                    hoveredGizmoAxis_ = GizmoAxis::Center;
                } else {
                    const auto& cam = scene_.ActiveCamera();
                    float dist = (sel->position - cam.position).length();
                    float arrowWorldLen = std::max(dist * 0.16f, 0.25f);

                    struct AxisDir { GizmoAxis axis; Vec3 dir; };
                    AxisDir axes[3] = {
                        { GizmoAxis::X, Vec3{1, 0, 0} },
                        { GizmoAxis::Y, Vec3{0, 1, 0} },
                        { GizmoAxis::Z, Vec3{0, 0, 1} }
                    };

                    float closestDist = 10.0f; // pixel threshold
                    for (const auto& a : axes) {
                        ImVec2 screenTip;
                        if (projectToScreen(sel->position + a.dir * arrowWorldLen, vp, screenTip)) {
                            float d = DistToSegment(io.MousePos, screenOrigin, screenTip);
                            if (d < closestDist) {
                                closestDist = d;
                                hoveredGizmoAxis_ = a.axis;
                            }
                        }
                    }
                }
            }
        }
    }

    auto tryPickNode = [&](SceneNode& node) -> bool {
        ImVec2 screen;
        if (!projectToScreen(node.position, vp, screen)) return false;
        float dx = io.MousePos.x - screen.x, dy = io.MousePos.y - screen.y;
        return (dx * dx + dy * dy) <= (12.0f * 12.0f);
    };

    auto pickSceneNode = [&]() -> SceneNode* {
        for (auto& cam : scene_.GetCameras()) {
            if (cam.get() != &scene_.ActiveCamera() && tryPickNode(*cam)) return cam.get();
        }
        for (auto& light : scene_.GetLights()) {
            if (tryPickNode(*light)) return light.get();
        }
        for (auto& model : scene_.GetModels()) {
            if (tryPickNode(model->transformHandle)) return &model->transformHandle;
        }
        return nullptr;
    };

    // 2. Right click context menu
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsWindowHovered()) {
        contextNode_ = pickSceneNode();
        if (contextNode_) ImGui::OpenPopup("Node Actions");
    }

    // 3. Left click to start gizmo drag or pick node
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered()) {
        if (hoveredGizmoAxis_ != GizmoAxis::None && sel) {
            activeGizmoAxis_ = hoveredGizmoAxis_;
            gizmoTargetNode_ = sel;
            gizmoDragStartPos_ = sel->position;
        } else {
            if (SceneNode* picked = pickSceneNode()) {
                scene_.SelectNode(picked);
                draggedNode_ = picked;
                picked->isDragging = true;
            } else {
                scene_.SelectNode(nullptr);
            }
        }
    }

    // 4. Handle active gizmo dragging along arrow axes
    if (activeGizmoAxis_ != GizmoAxis::None && gizmoTargetNode_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const auto& cam = scene_.ActiveCamera();
        float dist = (gizmoDragStartPos_ - cam.position).length();
        float arrowWorldLen = std::max(dist * 0.16f, 0.25f);

        if (activeGizmoAxis_ == GizmoAxis::Center) {
            Vec3 forward = cam.Forward();
            Vec3 flatForward = Vec3(forward.x, 0, forward.z).normalized();
            Vec3 right = Vec3::cross(flatForward, Vec3{0, 1, 0}).normalized();
            float dragScale = arrowWorldLen * 0.005f;
            gizmoTargetNode_->position = gizmoTargetNode_->position + right * (io.MouseDelta.x * dragScale);
            gizmoTargetNode_->position.y -= io.MouseDelta.y * dragScale;
        } else {
            Vec3 axisDir = (activeGizmoAxis_ == GizmoAxis::X) ? Vec3{1, 0, 0} :
                           ((activeGizmoAxis_ == GizmoAxis::Y) ? Vec3{0, 1, 0} : Vec3{0, 0, 1});

            ImVec2 sOrigin, sTip;
            if (projectToScreen(gizmoDragStartPos_, vp, sOrigin) &&
                projectToScreen(gizmoDragStartPos_ + axisDir * arrowWorldLen, vp, sTip)) {
                ImVec2 sAxis = { sTip.x - sOrigin.x, sTip.y - sOrigin.y };
                float sLen = std::sqrt(sAxis.x * sAxis.x + sAxis.y * sAxis.y);
                if (sLen > 1.0f) {
                    ImVec2 unitAxis = { sAxis.x / sLen, sAxis.y / sLen };
                    float mouseAlongAxis = io.MouseDelta.x * unitAxis.x + io.MouseDelta.y * unitAxis.y;
                    float worldDelta = mouseAlongAxis * (arrowWorldLen / sLen);
                    gizmoTargetNode_->position = gizmoTargetNode_->position + axisDir * worldDelta;
                }
            }
        }

        for (const auto& c : scene_.GetCameras()) {
            if (c.get() == gizmoTargetNode_) {
                c->SyncTargetFromOrientation();
                break;
            }
        }
    }

    // 5. Handle direct node dragging
    if (draggedNode_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const auto& cam = scene_.ActiveCamera();
        Vec3 forward = cam.Forward();
        Vec3 flatForward = Vec3(forward.x, 0, forward.z).normalized();
        Vec3 right = Vec3::cross(flatForward, Vec3{0, 1, 0}).normalized();
        float dragScale = 0.01f;
        draggedNode_->position = draggedNode_->position + right * (io.MouseDelta.x * dragScale);
        draggedNode_->position.y -= io.MouseDelta.y * dragScale;

        for (const auto& c : scene_.GetCameras()) {
            if (c.get() == draggedNode_) {
                c->SyncTargetFromOrientation();
                break;
            }
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        activeGizmoAxis_ = GizmoAxis::None;
        gizmoTargetNode_ = nullptr;
        if (draggedNode_) {
            draggedNode_->isDragging = false;
            draggedNode_ = nullptr;
        }
    }
}

void ViewportPanel::drawTransformOverlay() {
    SceneNode* node = scene_.SelectedNode();
    ImGui::SetCursorScreenPos(ImVec2(viewportOrigin_.x + 10.0f, viewportOrigin_.y + 38.0f));
    ImGui::BeginChild("transform_overlay", ImVec2(245, node ? 122 : 48), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!node) {
        ImGui::TextDisabled("LMB: select C / L / M");
        ImGui::TextDisabled("Drag colored arrows to move in 3D");
        ImGui::EndChild();
        return;
    }

    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Selected: %s", node->label.c_str());
    constexpr float step = 0.25f;
    auto move = [&](const char* label, float& component, float amount) {
        if (ImGui::Button(label, ImVec2(34, 0))) component += amount;
    };
    move("+X", node->position.x, step); ImGui::SameLine();
    move("-X", node->position.x, -step); ImGui::SameLine();
    move("+Y", node->position.y, step); ImGui::SameLine();
    move("-Y", node->position.y, -step); ImGui::SameLine();
    move("+Z", node->position.z, step); ImGui::SameLine();
    move("-Z", node->position.z, -step);
    ImGui::Text("%.2f, %.2f, %.2f", node->position.x, node->position.y, node->position.z);
    ImGui::TextDisabled("Drag 3D arrows (X=Red, Y=Green, Z=Blue)");
    ImGui::EndChild();
}

void ViewportPanel::drawModelsWireframe(ImDrawList* dl, const Mat4& vp) {
    const ImU32 wireColor = IM_COL32(160, 200, 255, 200);
    for (auto& model : scene_.GetModels()) {
        Mat4 mvp = Mat4::mul(vp, model->GetModelMatrix());
        const auto& verts = model->GetVertices();
        for (auto& sub : model->GetSubMeshes()) {
            for (size_t i = 0; i + 2 < sub.indices.size(); i += 3) {
                ImVec2 p0, p1, p2;
                bool ok0 = projectToScreen(verts[sub.indices[i + 0]].position, mvp, p0);
                bool ok1 = projectToScreen(verts[sub.indices[i + 1]].position, mvp, p1);
                bool ok2 = projectToScreen(verts[sub.indices[i + 2]].position, mvp, p2);
                if (ok0 && ok1) dl->AddLine(p0, p1, wireColor, 1.0f);
                if (ok1 && ok2) dl->AddLine(p1, p2, wireColor, 1.0f);
                if (ok2 && ok0) dl->AddLine(p2, p0, wireColor, 1.0f);
            }
        }
    }
}

void ViewportPanel::handleCameraFlight() {
    if (!ImGui::IsWindowFocused() || activeGizmoAxis_ != GizmoAxis::None || draggedNode_ != nullptr) return;

    ImGuiIO& io = ImGui::GetIO();
    float dt = io.DeltaTime;
    if (dt <= 0.0f) return;
    CameraNode& cam = scene_.ActiveCamera();

    if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        cam.yawDegrees += io.MouseDelta.x * 0.25f;
        cam.pitchDegrees = std::clamp(cam.pitchDegrees - io.MouseDelta.y * 0.25f, -89.0f, 89.0f);
    }

    float lookDelta = cam.lookSpeed * dt;
    if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))  cam.yawDegrees -= lookDelta;
    if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) cam.yawDegrees += lookDelta;
    if (ImGui::IsKeyDown(ImGuiKey_UpArrow))    cam.pitchDegrees = std::min(cam.pitchDegrees + lookDelta, 89.0f);
    if (ImGui::IsKeyDown(ImGuiKey_DownArrow))  cam.pitchDegrees = std::max(cam.pitchDegrees - lookDelta, -89.0f);

    Vec3 forward = cam.Forward();
    Vec3 flatForward = Vec3(forward.x, 0, forward.z).normalized();
    Vec3 right = Vec3::cross(flatForward, Vec3{0, 1, 0}).normalized();
    float moveDelta = cam.moveSpeed * dt;
    if (ImGui::IsKeyDown(ImGuiKey_W)) cam.position = cam.position + flatForward * moveDelta;
    if (ImGui::IsKeyDown(ImGuiKey_S)) cam.position = cam.position - flatForward * moveDelta;
    if (ImGui::IsKeyDown(ImGuiKey_A)) cam.position = cam.position - right * moveDelta;
    if (ImGui::IsKeyDown(ImGuiKey_D)) cam.position = cam.position + right * moveDelta;
    if (ImGui::IsKeyDown(ImGuiKey_Q)) cam.position.y -= moveDelta;
    if (ImGui::IsKeyDown(ImGuiKey_E)) cam.position.y += moveDelta;

    cam.SyncTargetFromOrientation();
}

void ViewportPanel::Draw(bool realtimePreview, float fps, ImTextureID sceneTexture) {
    ImGui::Begin("Viewport");

    viewportOrigin_ = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x > 4 && avail.y > 4) viewportSize_ = avail;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(viewportOrigin_, ImVec2(viewportOrigin_.x + viewportSize_.x, viewportOrigin_.y + viewportSize_.y),
                       IM_COL32(18, 18, 22, 255));

    if (sceneTexture != 0) {
        dl->AddImage(sceneTexture, viewportOrigin_, ImVec2(viewportOrigin_.x + viewportSize_.x, viewportOrigin_.y + viewportSize_.y));
    }

    float aspect = viewportSize_.y > 0 ? viewportSize_.x / viewportSize_.y : 1.0f;
    Mat4 view = BuildViewMatrix();
    Mat4 proj = BuildProjMatrix(aspect);
    Mat4 vp = Mat4::mul(proj, view);

    ImGui::InvisibleButton("viewport_input", viewportSize_, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    if (sceneTexture == 0) drawGrid(dl, vp);

    // Draw inactive cameras in 3D space
    for (const auto& cam : scene_.GetCameras()) {
        if (cam.get() != &scene_.ActiveCamera()) {
            drawNode(dl, *cam, vp);
            drawCameraDirection(dl, *cam, vp);
        }
    }

    // Draw all light nodes (orange 'L' markers) and spot cones
    for (const auto& light : scene_.GetLights()) {
        drawNode(dl, *light, vp);
        drawLightDirection(dl, *light, vp);
    }

    // Draw loaded models transform handles (green 'M' markers)
    for (auto& model : scene_.GetModels()) {
        drawNode(dl, model->transformHandle, vp);
    }

    // Draw 3D translation gizmo arrows for selected node
    drawGizmo(dl, vp);

    // Handle user interaction with gizmo and nodes
    handleGizmoInteraction(vp);

    drawTransformOverlay();

    if (ImGui::BeginPopup("Node Actions")) {
        if (contextNode_) {
            ImGui::Text("%s", contextNode_->label.c_str());
            if (ImGui::MenuItem("Select")) scene_.SelectNode(contextNode_);

            // Camera actions
            CameraNode* asCam = nullptr;
            for (const auto& c : scene_.GetCameras()) {
                if (c.get() == contextNode_) { asCam = c.get(); break; }
            }
            if (asCam) {
                if (ImGui::MenuItem("Look Through This Camera")) {
                    for (size_t i = 0; i < scene_.GetCameraCount(); ++i) {
                        if (&scene_.GetCamera(i) == asCam) {
                            scene_.SetActiveCameraIndex(i);
                            break;
                        }
                    }
                }
                if (scene_.GetCameraCount() > 1 && ImGui::MenuItem("Delete Camera")) {
                    scene_.RemoveCameraForNode(contextNode_);
                    contextNode_ = nullptr;
                }
            }

            // Light actions
            LightNode* asLight = nullptr;
            for (const auto& l : scene_.GetLights()) {
                if (l.get() == contextNode_) { asLight = l.get(); break; }
            }
            if (asLight) {
                if (ImGui::MenuItem(asLight->isSpot ? "Change to Point Light" : "Change to Spotlight")) {
                    asLight->isSpot = !asLight->isSpot;
                }
                if (scene_.GetLightCount() > 1 && ImGui::MenuItem("Delete Light")) {
                    scene_.RemoveLightForNode(contextNode_);
                    contextNode_ = nullptr;
                }
            }

            // Model actions
            bool isModel = (!asCam && !asLight);
            if (isModel) {
                if (ImGui::MenuItem("Delete Model")) {
                    scene_.RemoveModelForNode(contextNode_);
                    contextNode_ = nullptr;
                }
            }

            if (contextNode_ && ImGui::MenuItem("Reset Position")) {
                contextNode_->position = {0, asCam ? 1.5f : 0.0f, 0};
            }
        }
        ImGui::EndPopup();
    }

    if (realtimePreview) handleCameraFlight();

    // Feature 4: FPS + realtime state overlay
    char overlay[96];
    std::snprintf(overlay, sizeof(overlay), "%s  |  %.1f FPS  (K to toggle)",
                  realtimePreview ? "REALTIME" : "PAUSED", fps);
    dl->AddText(ImVec2(viewportOrigin_.x + 8, viewportOrigin_.y + 8),
                realtimePreview ? IM_COL32(120, 230, 140, 255) : IM_COL32(230, 190, 90, 255), overlay);

    ImGui::End();
}

