#pragma once
#include "../../scene/Scene.h"
#include <string>

// Feature 1: UI file picker to load .obj (+ associated .mtl textures/materials).
class ModelLoaderPanel {
public:
    explicit ModelLoaderPanel(Scene& scene) : scene_(scene) {}
    void Draw();

private:
    Scene& scene_;
    std::string lastError_;
    std::string lastLoadedPath_;

    void openFileDialogAndLoad();  // native Win32 GetOpenFileNameA
};
