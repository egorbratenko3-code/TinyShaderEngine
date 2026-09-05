#include "Scene.h"

Scene::Scene() {
    floor_.CreateDefaultFloor();
    AddCamera({0, 1.5f, 4.0f});
    AddLight({2.0f, 3.0f, 2.0f}, {1.0f, 0.75f, 0.4f}, 1.0f);
}

CameraNode* Scene::AddCamera(Vec3 pos) {
    size_t id = cameras_.size() + 1;
    auto cam = std::make_shared<CameraNode>(pos, "C" + (id > 1 ? std::to_string(id) : ""), "Camera " + std::to_string(id));
    cameras_.push_back(cam);
    return cam.get();
}

bool Scene::RemoveCamera(size_t index) {
    if (cameras_.size() <= 1 || index >= cameras_.size()) return false;
    if (selectedNode_ == cameras_[index].get()) selectedNode_ = nullptr;
    cameras_.erase(cameras_.begin() + index);
    if (activeCameraIndex_ >= cameras_.size()) {
        activeCameraIndex_ = cameras_.size() - 1;
    }
    return true;
}

bool Scene::RemoveCameraForNode(const SceneNode* node) {
    for (size_t i = 0; i < cameras_.size(); ++i) {
        if (cameras_[i].get() == node) {
            return RemoveCamera(i);
        }
    }
    return false;
}

LightNode* Scene::AddLight(Vec3 pos, Vec3 color, float intensity) {
    size_t id = lights_.size() + 1;
    auto light = std::make_shared<LightNode>(pos, color, intensity, "L" + (id > 1 ? std::to_string(id) : ""), "Light " + std::to_string(id));
    lights_.push_back(light);
    return light.get();
}

bool Scene::RemoveLight(size_t index) {
    if (lights_.size() <= 1 || index >= lights_.size()) return false;
    if (selectedNode_ == lights_[index].get()) selectedNode_ = nullptr;
    lights_.erase(lights_.begin() + index);
    return true;
}

bool Scene::RemoveLightForNode(const SceneNode* node) {
    for (size_t i = 0; i < lights_.size(); ++i) {
        if (lights_[i].get() == node) {
            return RemoveLight(i);
        }
    }
    return false;
}

bool Scene::LoadModel(const std::string& path, std::string& outError) {
    auto model = std::make_shared<Model>();
    if (!model->LoadFromFile(path, outError)) {
        return false;
    }
    if (gpuDevice_ != VK_NULL_HANDLE) {
        model->UploadToGPU(gpuDevice_, gpuPhysicalDevice_, gpuPool_, gpuQueue_);
    }
    models_.push_back(model);
    return true;
}

