#pragma once
#include "../core/Math.h"
#include "Node.h"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <cstdint>

struct Vertex {
    Vec3 position;
    Vec3 normal;
    float u = 0, v = 0;
};

struct MaterialTexture {
    std::string path;                 // resolved absolute path on disk
    int width = 0, height = 0, channels = 0;
    std::vector<uint8_t> pixels;      // loaded via stb_image, RGBA8
    bool loaded = false;
    // GPU copy used by the scene pass. Kept with the material so it has the
    // same lifetime as its source pixels/model.
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
};

struct Material {
    std::string name;
    Vec3 diffuseColor{0.8f, 0.8f, 0.8f};
    MaterialTexture diffuseTexture;   // "associated texture", Feature 1
    bool hasDiffuseTexture = false;
};

struct SubMesh {
    std::string materialName;
    int materialIndex = -1;           // index into Model::materials, -1 = none
    std::vector<uint32_t> indices;    // indexes into Model::vertices
};

// A loaded model: geometry (polygons), materials, and textures, from either
// .obj (+ .mtl, via tinyobjloader) or .fbx (via ufbx — see vendor/README.md;
// LoadFromFBX has NOT been compiled/tested against the real library, unlike
// everything else in this project — see the note in Model.cpp).
//
// GPU upload of geometry is real (UploadToGPU, used by ShadowMap and
// SceneRenderer). Material textures are loaded to CPU pixel data (stb_image)
// but not yet uploaded/sampled on the GPU — the shading pass still uses each
// material's flat diffuseColor. Wiring texture sampling into scene.frag is
// the natural next increment.
class Model {
public:
    bool LoadFromOBJ(const std::string& objPath, std::string& outError);
    bool LoadFromFBX(const std::string& fbxPath, std::string& outError);
    // Dispatches to LoadFromOBJ or LoadFromFBX based on the file extension.
    bool LoadFromFile(const std::string& path, std::string& outError);
    // Built-in finite ground plane. It is ordinary render geometry (not an
    // editor overlay), therefore receives shadows and participates in them.
    void CreateDefaultFloor(float halfExtent = 20.0f);

    const std::string& GetSourcePath() const { return sourcePath_; }
    const std::vector<Vertex>& GetVertices() const { return vertices_; }
    const std::vector<SubMesh>& GetSubMeshes() const { return subMeshes_; }
    const std::vector<Material>& GetMaterials() const { return materials_; }
    Vec3 GetBoundsMin() const { return boundsMin_; }
    Vec3 GetBoundsMax() const { return boundsMax_; }
    bool IsLoaded() const { return !vertices_.empty(); }
    bool IsFloor() const { return isFloor_; }

    // ---- Per-instance transform (free move + rotate, Feature request) ----
    // transformHandle.position is the model's world-space translation and is
    // draggable in the viewport (label "M"), same mechanism as Camera/Light.
    // rotationEulerDegrees is applied in X, then Y, then Z order.
    SceneNode transformHandle{ {0, 0, 0}, {0.35f, 0.85f, 0.45f}, "M" };
    Vec3 rotationEulerDegrees{0, 0, 0};
    Mat4 GetModelMatrix() const;

    // ---- GPU geometry (added for ShadowMap/SceneRenderer; the CPU
    // wireframe path in ViewportPanel/RenderExportPanel is independent and
    // still works without calling this). Combines all sub-meshes into one
    // index buffer with per-submesh {offset,count} so a pass can draw the
    // whole model in one bind + N indexed draws.
    void UploadToGPU(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool pool, VkQueue queue);
    void ReleaseGPU(VkDevice device);
    bool IsUploadedToGPU() const { return vertexBuffer_ != VK_NULL_HANDLE; }
    VkBuffer GetVertexBuffer() const { return vertexBuffer_; }
    VkBuffer GetIndexBuffer() const { return indexBuffer_; }
    struct GpuSubMeshRange { uint32_t indexOffset, indexCount; };
    const std::vector<GpuSubMeshRange>& GetGpuSubMeshRanges() const { return gpuRanges_; }

    ~Model();

private:
    std::string sourcePath_;
    std::vector<Vertex> vertices_;
    std::vector<SubMesh> subMeshes_;
    std::vector<Material> materials_;
    bool isFloor_ = false;
    Vec3 boundsMin_{0,0,0}, boundsMax_{0,0,0};

    // GPU geometry state (VK_NULL_HANDLE until UploadToGPU is called)
    VkDevice gpuDevice_ = VK_NULL_HANDLE; // stashed only so the destructor can clean up safely
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory_ = VK_NULL_HANDLE;
    std::vector<GpuSubMeshRange> gpuRanges_;

    void computeBounds();
    void generateMissingNormals();
    void loadMaterialTexture(MaterialTexture& tex, const std::string& baseDir);
    void uploadMaterialTexture(MaterialTexture& tex, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool pool, VkQueue queue);
};
