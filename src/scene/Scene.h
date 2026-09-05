#pragma once
#include "Node.h"
#include "Model.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <string>

class Scene {
public:
    // ---- Camera management ----
    size_t GetCameraCount() const { return cameras_.size(); }
    CameraNode& GetCamera(size_t index) { return *cameras_[index]; }
    const CameraNode& GetCamera(size_t index) const { return *cameras_[index]; }
    CameraNode& ActiveCamera() { return *cameras_[activeCameraIndex_]; }
    const CameraNode& ActiveCamera() const { return *cameras_[activeCameraIndex_]; }
    size_t GetActiveCameraIndex() const { return activeCameraIndex_; }
    void SetActiveCameraIndex(size_t index) { if (index < cameras_.size()) activeCameraIndex_ = index; }
    CameraNode* AddCamera(Vec3 pos = {0, 1.5f, 4.0f});
    bool RemoveCamera(size_t index);
    bool RemoveCameraForNode(const SceneNode* node);
    const std::vector<std::shared_ptr<CameraNode>>& GetCameras() const { return cameras_; }

    // ---- Light management ----
    size_t GetLightCount() const { return lights_.size(); }
    LightNode& GetLight(size_t index) { return *lights_[index]; }
    const LightNode& GetLight(size_t index) const { return *lights_[index]; }
    LightNode& PrimaryLight() { return *lights_[0]; }
    const LightNode& PrimaryLight() const { return *lights_[0]; }
    LightNode* AddLight(Vec3 pos = {2.0f, 3.0f, 2.0f}, Vec3 color = {1.0f, 0.75f, 0.4f}, float intensity = 1.0f);
    bool RemoveLight(size_t index);
    bool RemoveLightForNode(const SceneNode* node);
    const std::vector<std::shared_ptr<LightNode>>& GetLights() const { return lights_; }

    // Set once after Vulkan is initialized (Application::Init) so newly
    // loaded models can be uploaded to GPU buffers immediately — needed by
    // ShadowMap, which draws real geometry rather than the CPU wireframe.
    void SetGpuUploadContext(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool pool, VkQueue queue) {
        gpuDevice_ = device; gpuPhysicalDevice_ = physicalDevice; gpuPool_ = pool; gpuQueue_ = queue;
        floor_.UploadToGPU(device, physicalDevice, pool, queue);
    }

    // Loads a model (.obj or .fbx, dispatched by extension) and adds it to
    // the scene. Returns false + fills outError on failure. If a GPU upload
    // context has been set, the model is uploaded to GPU buffers right away.
    bool LoadModel(const std::string& path, std::string& outError);

    void ClearModels() {
        if (gpuDevice_ != VK_NULL_HANDLE) vkDeviceWaitIdle(gpuDevice_);
        models_.clear();
        selectedNode_ = nullptr;
    }
    void ReleaseGpuResources() {
        if (gpuDevice_ == VK_NULL_HANDLE) return;
        vkDeviceWaitIdle(gpuDevice_);
        for (auto& model : models_) model->ReleaseGPU(gpuDevice_);
        floor_.ReleaseGPU(gpuDevice_);
        gpuDevice_ = VK_NULL_HANDLE;
    }
    bool IsEmpty() const { return models_.empty(); } // -> viewport shows grid (Feature 2)
    const std::vector<std::shared_ptr<Model>>& GetModels() const { return models_; }
    // Editor selection is a non-owning reference to one of the stable scene
    // nodes (camera, light, or a model transform handle).
    void SelectNode(SceneNode* node) { selectedNode_ = node; }
    SceneNode* SelectedNode() const { return selectedNode_; }
    bool RemoveModelForNode(const SceneNode* node) {
        for (auto it = models_.begin(); it != models_.end(); ++it) {
            if (&(*it)->transformHandle == node) {
                if (selectedNode_ == node) selectedNode_ = nullptr;
                if (gpuDevice_ != VK_NULL_HANDLE) vkDeviceWaitIdle(gpuDevice_);
                models_.erase(it);
                return true;
            }
        }
        return false;
    }
    const Model& GetFloor() const { return floor_; }
    Model& GetFloor() { return floor_; }

    Scene();

private:
    std::vector<std::shared_ptr<CameraNode>> cameras_;
    size_t activeCameraIndex_ = 0;
    std::vector<std::shared_ptr<LightNode>> lights_;
    std::vector<std::shared_ptr<Model>> models_;
    SceneNode* selectedNode_ = nullptr;
    Model floor_;
    VkDevice gpuDevice_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpuPhysicalDevice_ = VK_NULL_HANDLE;
    VkCommandPool gpuPool_ = VK_NULL_HANDLE;
    VkQueue gpuQueue_ = VK_NULL_HANDLE;
};
