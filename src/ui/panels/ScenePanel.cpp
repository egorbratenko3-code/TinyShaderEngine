#include "ScenePanel.h"
#include "imgui.h"

void ScenePanel::Draw() {
    ImGui::Begin("Scene");

    // =========================================================================
    // Cameras Section
    // =========================================================================
    char camHeader[64];
    std::snprintf(camHeader, sizeof(camHeader), "Cameras (%d)", (int)scene_.GetCameraCount());
    if (ImGui::CollapsingHeader(camHeader, ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("+ Add Camera", ImVec2(-1, 0))) {
            const auto& cur = scene_.ActiveCamera();
            Vec3 newPos = cur.position + cur.Forward() * 2.0f + Vec3{1.0f, 0.0f, 0.0f};
            CameraNode* newCam = scene_.AddCamera(newPos);
            newCam->yawDegrees = cur.yawDegrees;
            newCam->pitchDegrees = cur.pitchDegrees;
            newCam->SyncTargetFromOrientation();
            scene_.SelectNode(newCam);
        }

        ImGui::Separator();
        for (size_t i = 0; i < scene_.GetCameraCount(); ++i) {
            CameraNode& cam = scene_.GetCamera(i);
            bool isActive = (i == scene_.GetActiveCameraIndex());
            bool isSelected = (scene_.SelectedNode() == &cam);

            ImGui::PushID((int)i);
            char nodeTitle[64];
            std::snprintf(nodeTitle, sizeof(nodeTitle), "%s %s", cam.name.c_str(), isActive ? "[ACTIVE VIEW]" : "");
            if (isActive) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.75f, 1.0f, 1.0f));

            bool open = ImGui::TreeNodeEx((void*)(intptr_t)i,
                ImGuiTreeNodeFlags_OpenOnArrow | (isSelected ? ImGuiTreeNodeFlags_Selected : 0) | (isActive ? ImGuiTreeNodeFlags_DefaultOpen : 0),
                "%s", nodeTitle);
            if (isActive) ImGui::PopStyleColor();

            if (open) {
                if (!isActive) {
                    if (ImGui::Button("Look Through This Camera")) {
                        scene_.SetActiveCameraIndex(i);
                    }
                    ImGui::SameLine();
                }
                if (ImGui::Button(isSelected ? "Selected in Viewport" : "Select Gizmo")) {
                    scene_.SelectNode(&cam);
                }
                if (scene_.GetCameraCount() > 1) {
                    ImGui::SameLine();
                    if (ImGui::Button("Delete")) {
                        scene_.RemoveCamera(i);
                        ImGui::TreePop();
                        ImGui::PopID();
                        break;
                    }
                }

                ImGui::DragFloat3("Position", &cam.position.x, 0.05f);
                ImGui::SliderFloat("FOV (deg)", &cam.fovDegrees, 10.0f, 120.0f);
                ImGui::SliderFloat("Move Speed", &cam.moveSpeed, 0.5f, 20.0f);
                ImGui::SliderFloat("Look Speed", &cam.lookSpeed, 10.0f, 180.0f);
                if (ImGui::SmallButton("Reset Orientation")) {
                    cam.yawDegrees = -90.0f;
                    cam.pitchDegrees = -20.5f;
                    cam.SyncTargetFromOrientation();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Controls for active camera (click Viewport first):");
        ImGui::BulletText("WASD + Q/E: fly move");
        ImGui::BulletText("Arrow keys / RMB drag: rotate view");
        ImGui::BulletText("Move with 3D gizmo arrows (Red/Green/Blue)");
    }

    ImGui::Spacing();

    // =========================================================================
    // Lights Section
    // =========================================================================
    char lightHeader[64];
    std::snprintf(lightHeader, sizeof(lightHeader), "Lights (%d)", (int)scene_.GetLightCount());
    if (ImGui::CollapsingHeader(lightHeader, ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("+ Add Light (Orange 'L')", ImVec2(-1, 0))) {
            const auto& cam = scene_.ActiveCamera();
            Vec3 newPos = cam.position + cam.Forward() * 3.0f + Vec3{0.0f, 1.0f, 0.0f};
            LightNode* newLight = scene_.AddLight(newPos, {1.0f, 0.85f, 0.55f}, 1.5f);
            scene_.SelectNode(newLight);
        }

        ImGui::Separator();
        for (size_t i = 0; i < scene_.GetLightCount(); ++i) {
            LightNode& light = scene_.GetLight(i);
            bool isSelected = (scene_.SelectedNode() == &light);

            ImGui::PushID((int)(1000 + i));
            char lightTitle[64];
            std::snprintf(lightTitle, sizeof(lightTitle), "%s [%s]", light.name.c_str(), light.isSpot ? "Spot" : "Point");

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(light.colorTint.x, light.colorTint.y, light.colorTint.z, 1.0f));
            bool open = ImGui::TreeNodeEx((void*)(intptr_t)(1000 + i),
                ImGuiTreeNodeFlags_OpenOnArrow | (isSelected ? ImGuiTreeNodeFlags_Selected : 0) | (i == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0),
                "%s", lightTitle);
            ImGui::PopStyleColor();

            if (open) {
                if (ImGui::Button(isSelected ? "Selected in Viewport" : "Select Gizmo")) {
                    scene_.SelectNode(&light);
                }
                if (scene_.GetLightCount() > 1) {
                    ImGui::SameLine();
                    if (ImGui::Button("Delete Light")) {
                        scene_.RemoveLight(i);
                        ImGui::TreePop();
                        ImGui::PopID();
                        break;
                    }
                }

                ImGui::DragFloat3("Position", &light.position.x, 0.05f);
                ImGui::ColorEdit3("Color", &light.colorTint.x);
                ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 10.0f);
                ImGui::Checkbox("Spotlight (aimed cone)", &light.isSpot);
                if (light.isSpot) {
                    ImGui::SliderFloat("Aim Yaw (deg)", &light.aimYawDegrees, -180.0f, 180.0f);
                    ImGui::SliderFloat("Aim Pitch (deg)", &light.aimPitchDegrees, -89.0f, 89.0f);
                    ImGui::SliderFloat("Cone Angle (deg)", &light.spotConeDegrees, 5.0f, 89.0f);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Light 1 casts real-time soft shadows via ShadowMap.");
        ImGui::TextDisabled("All lights illuminate models with Blinn-Phong highlights.");
    }

    ImGui::Spacing();

    // =========================================================================
    // Selected Object & 3D Gizmo
    // =========================================================================
    if (ImGui::CollapsingHeader("Selected Object & Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        SceneNode* node = scene_.SelectedNode();
        if (!node) {
            ImGui::TextDisabled("Click any Camera (C), Light (L), or Model (M) in the viewport.");
            ImGui::TextDisabled("Visible Red (X), Green (Y), Blue (Z) arrows will appear to move it!");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Selected: %s", node->label.c_str());
            ImGui::DragFloat3("Position", &node->position.x, 0.05f);

            constexpr float step = 0.25f;
            if (ImGui::Button("+X")) { node->position.x += step; } ImGui::SameLine();
            if (ImGui::Button("-X")) { node->position.x -= step; } ImGui::SameLine();
            if (ImGui::Button("+Y")) { node->position.y += step; } ImGui::SameLine();
            if (ImGui::Button("-Y")) { node->position.y -= step; } ImGui::SameLine();
            if (ImGui::Button("+Z")) { node->position.z += step; } ImGui::SameLine();
            if (ImGui::Button("-Z")) { node->position.z -= step; }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "3D Gizmo Active in Viewport:");
            ImGui::BulletText("Red arrow: drag along X axis");
            ImGui::BulletText("Green arrow: drag along Y axis");
            ImGui::BulletText("Blue arrow: drag along Z axis");
            ImGui::BulletText("Center disc: free move in camera plane");

            for (const auto& c : scene_.GetCameras()) {
                if (c.get() == node) {
                    c->SyncTargetFromOrientation();
                    break;
                }
            }
        }
    }

    ImGui::End();
}

