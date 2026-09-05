#include "ModelLoaderPanel.h"
#include "imgui.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

void ModelLoaderPanel::openFileDialogAndLoad() {
    char filePath[MAX_PATH] = {0};

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr; // set to your GLFW window's native HWND if you want it modal-to-app
    ofn.lpstrFilter = "3D Models (*.obj;*.fbx)\0*.obj;*.fbx\0Wavefront OBJ (*.obj)\0*.obj\0Autodesk FBX (*.fbx)\0*.fbx\0All Files\0*.*\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = "Load Model (.obj / .fbx)";

    if (GetOpenFileNameA(&ofn)) {
        std::string path(filePath);
        lastError_.clear();
        if (scene_.LoadModel(path, lastError_)) {
            lastLoadedPath_ = path;
        } else {
            lastLoadedPath_.clear();
        }
    }
}

void ModelLoaderPanel::Draw() {
    ImGui::Begin("Model Loader");

    if (ImGui::Button("Load Model (.obj/.fbx)...", ImVec2(-1, 0))) {
        openFileDialogAndLoad();
    }

    ImGui::Separator();

    if (!lastError_.empty()) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Load failed:");
        ImGui::TextWrapped("%s", lastError_.c_str());
    }

    if (scene_.GetModels().empty()) {
        ImGui::TextDisabled("No models loaded. Scene shows grid only.");
    } else {
        ImGui::Text("Loaded models: %d", (int)scene_.GetModels().size());
        if (ImGui::Button("Clear Scene")) scene_.ClearModels();

        for (auto& model : scene_.GetModels()) {
            ImGui::PushID(model.get());
            if (ImGui::CollapsingHeader(model->GetSourcePath().c_str())) {
                ImGui::BulletText("Vertices: %d", (int)model->GetVertices().size());
                ImGui::BulletText("Sub-meshes: %d", (int)model->GetSubMeshes().size());
                ImGui::BulletText("Materials: %d", (int)model->GetMaterials().size());

                ImGui::Separator();
                ImGui::TextDisabled("Transform (drag the green 'M' marker in the viewport to move)");
                ImGui::DragFloat3("Position", &model->transformHandle.position.x, 0.05f);
                ImGui::DragFloat3("Rotation (deg)", &model->rotationEulerDegrees.x, 1.0f, -180.0f, 180.0f);
                if (ImGui::SmallButton("Reset Transform")) {
                    model->transformHandle.position = Vec3(0, 0, 0);
                    model->rotationEulerDegrees = Vec3(0, 0, 0);
                }
                ImGui::Separator();

                for (auto& mat : model->GetMaterials()) {
                    ImGui::Indent();
                    ImGui::ColorButton(("##" + mat.name).c_str(),
                        ImVec4(mat.diffuseColor.x, mat.diffuseColor.y, mat.diffuseColor.z, 1.0f),
                        0, ImVec2(14, 14));
                    ImGui::SameLine();
                    if (mat.hasDiffuseTexture) {
                        ImGui::Text("%s  [texture: %dx%d %s]", mat.name.c_str(),
                            mat.diffuseTexture.width, mat.diffuseTexture.height,
                            mat.diffuseTexture.loaded ? "loaded" : "FAILED TO LOAD");
                    } else {
                        ImGui::Text("%s  [no texture]", mat.name.c_str());
                    }
                    ImGui::Unindent();
                }
            }
            ImGui::PopID();
        }
    }

    ImGui::End();
}
