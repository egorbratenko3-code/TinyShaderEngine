#include "UIManager.h"
#include "imgui.h"
#include "imgui_internal.h"

void UIManager::drawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            ImGui::MenuItem("Load .obj...  (see Model Loader panel)", nullptr, false, false);
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) { /* left to window-close handling */ }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Viewport", nullptr, false, false);
            ImGui::MenuItem("Model Loader", nullptr, false, false);
            ImGui::MenuItem("HQ Render & Export", nullptr, false, false);
            ImGui::MenuItem("Effects", nullptr, false, false);
            ImGui::MenuItem("Scene", nullptr, false, false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::Text("TinyShaderEngine v1.0");
            ImGui::Text("K - toggle realtime preview");
            ImGui::Text("Click Viewport, then: WASD+Q/E move camera, arrows look");
            ImGui::Text("Drag markers: C = camera, L = light, M = object");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void UIManager::SetUpDockspace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("DockHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("TSE_Dockspace");

    if (!dockspaceBuilt_) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID center = dockspaceId;
        ImGuiID left   = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,  0.20f, nullptr, &center);
        ImGuiID right  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
        ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, nullptr, &left);
        ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, nullptr, &right);

        ImGui::DockBuilderDockWindow("Model Loader", left);
        ImGui::DockBuilderDockWindow("HQ Render & Export", leftBottom);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderDockWindow("Effects", right);
        ImGui::DockBuilderDockWindow("Scene", rightBottom);
        ImGui::DockBuilderFinish(dockspaceId);
        dockspaceBuilt_ = true;
    }

    ImGui::DockSpace(dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_None);

    drawMenuBar();
    ImGui::End();
}

void UIManager::Draw(bool realtimePreview, float fps, ImTextureID sceneTexture) {
    SetUpDockspace();
    viewport_.Draw(realtimePreview, fps, sceneTexture);
    modelLoader_.Draw();
    renderExport_.Draw();
    effectsPanel_.Draw();
    scenePanel_.Draw();
}
