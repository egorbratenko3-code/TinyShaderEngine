#include "Model.h"
#include "../core/VulkanUtils.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ufbx: NOT vendored by us (see vendor/README.md — MIT licensed, single
// ufbx.h + ufbx.c pair, get it from https://github.com/ufbx/ufbx). Unlike
// every other integration in this project, LoadFromFBX() below has NOT been
// compiled or run against the real library — see the comment on that
// function for specifics on what to check first if it doesn't build clean.
#include "ufbx.h"

#include <filesystem>
#include <limits>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace fs = std::filesystem;

Mat4 Model::GetModelMatrix() const {
    constexpr float kDeg2Rad = 3.14159265f / 180.0f;
    Mat4 rx = Mat4::rotationX(rotationEulerDegrees.x * kDeg2Rad);
    Mat4 ry = Mat4::rotationY(rotationEulerDegrees.y * kDeg2Rad);
    Mat4 rz = Mat4::rotationZ(rotationEulerDegrees.z * kDeg2Rad);
    Mat4 rot = Mat4::mul(rz, Mat4::mul(ry, rx)); // apply X, then Y, then Z
    return Mat4::mul(Mat4::translation(transformHandle.position), rot);
}

bool Model::LoadFromFile(const std::string& path, std::string& outError) {
    fs::path p(path);
    std::string ext = p.has_extension() ? p.extension().string() : "";
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    if (ext == ".fbx") return LoadFromFBX(path, outError);
    if (ext == ".obj") return LoadFromOBJ(path, outError);

    outError = "Unsupported file extension '" + ext + "' (supported: .obj, .fbx)";
    return false;
}

void Model::CreateDefaultFloor(float halfExtent) {
    vertices_.clear(); subMeshes_.clear(); materials_.clear(); isFloor_ = true;
    // Counter-clockwise from above: a subtle matte floor, deliberately with
    // no grid-line texture so editor guides can never enter the render.
    vertices_ = {
        {{-halfExtent, 0, -halfExtent}, {0,1,0}, 0,0},
        {{ halfExtent, 0, -halfExtent}, {0,1,0}, 1,0},
        {{ halfExtent, 0,  halfExtent}, {0,1,0}, 1,1},
        {{-halfExtent, 0,  halfExtent}, {0,1,0}, 0,1},
    };
    Material material; material.name = "Default floor"; material.diffuseColor = {0.22f, 0.24f, 0.28f}; materials_.push_back(material);
    // Counter-clockwise when viewed from above (+Y); the scene pipeline
    // culls back faces, so winding must match the upward normal.
    SubMesh mesh; mesh.materialName = material.name; mesh.materialIndex = 0; mesh.indices = {0,3,2, 0,2,1}; subMeshes_.push_back(mesh);
    generateMissingNormals();
    computeBounds();
}

bool Model::LoadFromOBJ(const std::string& objPath, std::string& outError) {
    isFloor_ = false;
    vertices_.clear();
    subMeshes_.clear();
    materials_.clear();

    fs::path p(objPath);
    std::string baseDir = p.has_parent_path() ? p.parent_path().string() + "/" : "./";

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> tobjMaterials;
    std::string warn, err;

    bool ok = tinyobj::LoadObj(&attrib, &shapes, &tobjMaterials, &warn, &err,
                                objPath.c_str(), baseDir.c_str(), /*triangulate=*/true);
    if (!warn.empty()) { /* non-fatal, surfaced to UI via outError below if needed */ }
    if (!ok) {
        outError = err.empty() ? "Unknown error parsing OBJ" : err;
        return false;
    }

    sourcePath_ = objPath;

    // --- Materials + associated textures --------------------------------
    materials_.reserve(tobjMaterials.size());
    for (auto& tm : tobjMaterials) {
        Material mat;
        mat.name = tm.name;
        mat.diffuseColor = Vec3(tm.diffuse[0], tm.diffuse[1], tm.diffuse[2]);
        if (!tm.diffuse_texname.empty()) {
            mat.hasDiffuseTexture = true;
            mat.diffuseTexture.path = baseDir + tm.diffuse_texname;
            loadMaterialTexture(mat.diffuseTexture, baseDir);
        }
        materials_.push_back(std::move(mat));
    }

    // --- Geometry: dedupe vertices per (pos,normal,uv) triple ------------
    struct KeyHash { size_t operator()(const std::string& s) const { return std::hash<std::string>{}(s); } };
    std::unordered_map<std::string, uint32_t> uniqueVerts;

    for (const auto& shape : shapes) {
        SubMesh sub;
        sub.materialName = "default";
        // tinyobj gives one material id per face; obj files commonly use one
        // material per shape/group in practice, so take the first face's id.
        if (!shape.mesh.material_ids.empty() && shape.mesh.material_ids[0] >= 0) {
            sub.materialIndex = shape.mesh.material_ids[0];
            sub.materialName = tobjMaterials[sub.materialIndex].name;
        }

        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int fv = shape.mesh.num_face_vertices[f];
            for (int v = 0; v < fv; v++) {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];

                Vertex vert{};
                vert.position = Vec3(
                    attrib.vertices[3*idx.vertex_index + 0],
                    attrib.vertices[3*idx.vertex_index + 1],
                    attrib.vertices[3*idx.vertex_index + 2]);

                if (idx.normal_index >= 0) {
                    vert.normal = Vec3(
                        attrib.normals[3*idx.normal_index + 0],
                        attrib.normals[3*idx.normal_index + 1],
                        attrib.normals[3*idx.normal_index + 2]);
                }
                if (idx.texcoord_index >= 0) {
                    vert.u = attrib.texcoords[2*idx.texcoord_index + 0];
                    vert.v = attrib.texcoords[2*idx.texcoord_index + 1];
                }

                std::string key = std::to_string(idx.vertex_index) + "/" +
                                   std::to_string(idx.normal_index) + "/" +
                                   std::to_string(idx.texcoord_index);
                auto it = uniqueVerts.find(key);
                uint32_t vIndex;
                if (it == uniqueVerts.end()) {
                    vIndex = static_cast<uint32_t>(vertices_.size());
                    vertices_.push_back(vert);
                    uniqueVerts.emplace(key, vIndex);
                } else {
                    vIndex = it->second;
                }
                sub.indices.push_back(vIndex);
            }
            indexOffset += fv;
        }
        subMeshes_.push_back(std::move(sub));
    }

    generateMissingNormals();
    computeBounds();
    return true;
}

// ---- FBX loading via ufbx --------------------------------------------------
// CAUTION: this function is written against ufbx's documented public API
// from reference knowledge, not compiled or run against the actual library
// (unlike every other integration in this project — tinyobjloader, stb,
// GLFW, ImGui — which follow well-established, stable, widely-seen patterns
// I have high confidence in). If this fails to compile:
//   - Check the exact field names on ufbx_mesh / ufbx_vertex_vec3 /
//     ufbx_material against the ufbx.h you vendored (its API has evolved
//     across versions; struct field names below match a fairly recent one).
//   - ufbx_load_file/ufbx_free_scene/ufbx_triangulate_face are the core
//     entry points and are the most likely to still be correct even if
//     minor struct fields have shifted.
// Report the exact compiler error and I can correct field names precisely.
bool Model::LoadFromFBX(const std::string& fbxPath, std::string& outError) {
    vertices_.clear();
    subMeshes_.clear();
    materials_.clear();

    fs::path p(fbxPath);
    std::string baseDir = p.has_parent_path() ? p.parent_path().string() + "/" : "./";

    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up; // match our Vec3/Mat4 convention
    opts.target_unit_meters = 1.0f;

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(fbxPath.c_str(), &opts, &error);
    if (!scene) {
        outError = std::string("ufbx: ") + (error.description.data ? error.description.data : "failed to load FBX file");
        return false;
    }

    sourcePath_ = fbxPath;

    // --- Materials + associated textures --------------------------------
    materials_.reserve(scene->materials.count);
    std::unordered_map<const ufbx_material*, int> materialIndexByPtr;
    for (size_t i = 0; i < scene->materials.count; i++) {
        ufbx_material* m = scene->materials.data[i];
        Material mat;
        mat.name = m->name.data ? std::string(m->name.data, m->name.length) : ("material_" + std::to_string(i));

        // Classic FBX Phong/Lambert diffuse color; falls back to PBR base
        // color if a material only defines the PBR side of ufbx's model.
        if (m->fbx.diffuse_color.has_value) {
            ufbx_vec3 c = m->fbx.diffuse_color.value_vec3;
            mat.diffuseColor = Vec3((float)c.x, (float)c.y, (float)c.z);
        } else if (m->pbr.base_color.has_value) {
            ufbx_vec3 c = m->pbr.base_color.value_vec3;
            mat.diffuseColor = Vec3((float)c.x, (float)c.y, (float)c.z);
        }

        ufbx_texture* tex = m->fbx.diffuse_color.texture;
        if (!tex && m->pbr.base_color.texture) tex = m->pbr.base_color.texture;
        if (tex && tex->filename.data) {
            mat.hasDiffuseTexture = true;
            std::string texPath = std::string(tex->filename.data, tex->filename.length);
            // FBX often stores absolute paths from the original authoring
            // machine; fall back to the FBX's own directory + the filename
            // if that absolute path doesn't resolve locally.
            fs::path asPath(texPath);
            if (!fs::exists(asPath)) {
                asPath = fs::path(baseDir) / asPath.filename();
            }
            mat.diffuseTexture.path = asPath.string();
            loadMaterialTexture(mat.diffuseTexture, baseDir);
        }

        materialIndexByPtr[m] = (int)materials_.size();
        materials_.push_back(std::move(mat));
    }

    // --- Geometry ---------------------------------------------------------
    for (size_t mi = 0; mi < scene->meshes.count; mi++) {
        ufbx_mesh* mesh = scene->meshes.data[mi];

        // One SubMesh per material used on this mesh (ufbx already groups
        // faces by material via mesh->material_parts).
        for (size_t partIdx = 0; partIdx < mesh->material_parts.count; partIdx++) {
            ufbx_mesh_part& part = mesh->material_parts.data[partIdx];
            if (part.num_faces == 0) continue;

            SubMesh sub;
            sub.materialName = "default";
            sub.materialIndex = -1;
            if (partIdx < mesh->materials.count && mesh->materials.data[partIdx]) {
                ufbx_material* mat = mesh->materials.data[partIdx];
                auto it = materialIndexByPtr.find(mat);
                if (it != materialIndexByPtr.end()) {
                    sub.materialIndex = it->second;
                    sub.materialName = materials_[it->second].name;
                }
            }

            std::vector<uint32_t> triIndices(mesh->max_face_triangles * 3);

            for (size_t fi = 0; fi < part.num_faces; fi++) {
                ufbx_face face = mesh->faces.data[part.face_indices.data[fi]];
                uint32_t numTris = ufbx_triangulate_face(triIndices.data(), triIndices.size(), mesh, face);

                for (uint32_t t = 0; t < numTris * 3; t++) {
                    uint32_t vIdx = triIndices[t];

                    Vertex vert{};
                    ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, vIdx);
                    vert.position = Vec3((float)pos.x, (float)pos.y, (float)pos.z);

                    if (mesh->vertex_normal.exists) {
                        ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, vIdx);
                        vert.normal = Vec3((float)n.x, (float)n.y, (float)n.z);
                    }
                    if (mesh->vertex_uv.exists) {
                        ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, vIdx);
                        vert.u = (float)uv.x; vert.v = (float)uv.y;
                    }

                    uint32_t newIndex = (uint32_t)vertices_.size();
                    vertices_.push_back(vert);
                    sub.indices.push_back(newIndex);
                }
            }
            if (!sub.indices.empty()) subMeshes_.push_back(std::move(sub));
        }
    }

    ufbx_free_scene(scene);

    generateMissingNormals();
    computeBounds();
    return true;
}

void Model::loadMaterialTexture(MaterialTexture& tex, const std::string& /*baseDir*/) {
    int w, h, ch;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* data = stbi_load(tex.path.c_str(), &w, &h, &ch, 4 /*force RGBA*/);
    if (!data) {
        tex.loaded = false;
        return;
    }
    tex.width = w; tex.height = h; tex.channels = 4;
    tex.pixels.assign(data, data + (size_t)w * h * 4);
    stbi_image_free(data);
    tex.loaded = true;
    // NOTE: pixels are loaded into CPU memory here (satisfies "load
    // associated textures" for Feature 1). GPU upload (VkImage + sampler +
    // descriptor set) is part of the deferred shading pipeline work.
}

void Model::uploadMaterialTexture(MaterialTexture& tex, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool pool, VkQueue queue) {
    if (!tex.loaded || tex.image || tex.pixels.empty()) return;
    const VkDeviceSize bytes = VkDeviceSize(tex.width) * tex.height * 4;
    VkBuffer staging{}; VkDeviceMemory stagingMemory{};
    vkutil::CreateBuffer(device, physicalDevice, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMemory);
    void* mapped{}; vkMapMemory(device, stagingMemory, 0, bytes, 0, &mapped);
    std::memcpy(mapped, tex.pixels.data(), (size_t)bytes); vkUnmapMemory(device, stagingMemory);
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D; ici.format = VK_FORMAT_R8G8B8A8_SRGB; ici.extent = {(uint32_t)tex.width, (uint32_t)tex.height, 1};
    ici.mipLevels = 1; ici.arrayLayers = 1; ici.samples = VK_SAMPLE_COUNT_1_BIT; ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(device, &ici, nullptr, &tex.image);
    VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device, tex.image, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = req.size;
    mai.memoryTypeIndex = vkutil::FindMemoryType(physicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device, &mai, nullptr, &tex.memory); vkBindImageMemory(device, tex.image, tex.memory, 0);
    VkCommandBuffer cmd = vkutil::BeginSingleTimeCommands(device, pool);
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.image = tex.image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,nullptr,0,nullptr,1,&barrier);
    VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent = {(uint32_t)tex.width,(uint32_t)tex.height,1};
    vkCmdCopyBufferToImage(cmd, staging, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,nullptr,0,nullptr,1,&barrier);
    vkutil::EndSingleTimeCommands(device, pool, queue, cmd);
    vkDestroyBuffer(device, staging, nullptr); vkFreeMemory(device, stagingMemory, nullptr);
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vci.image = tex.image; vci.viewType = VK_IMAGE_VIEW_TYPE_2D; vci.format = VK_FORMAT_R8G8B8A8_SRGB; vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCreateImageView(device, &vci, nullptr, &tex.view);
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT; sci.maxLod = 1.0f;
    vkCreateSampler(device, &sci, nullptr, &tex.sampler);
}

void Model::computeBounds() {
    if (vertices_.empty()) { boundsMin_ = boundsMax_ = Vec3(0,0,0); return; }
    Vec3 mn(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    Vec3 mx(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
    for (auto& v : vertices_) {
        mn.x = std::min(mn.x, v.position.x); mx.x = std::max(mx.x, v.position.x);
        mn.y = std::min(mn.y, v.position.y); mx.y = std::max(mx.y, v.position.y);
        mn.z = std::min(mn.z, v.position.z); mx.z = std::max(mx.z, v.position.z);
    }
    boundsMin_ = mn; boundsMax_ = mx;
}

void Model::generateMissingNormals() {
    // OBJ files frequently omit `vn` data. A zero normal makes a Lambert
    // material completely black, so derive smooth-enough face normals only
    // for vertices that were missing one.
    for (const auto& sub : subMeshes_) {
        for (size_t i = 0; i + 2 < sub.indices.size(); i += 3) {
            Vertex& a = vertices_[sub.indices[i]];
            Vertex& b = vertices_[sub.indices[i + 1]];
            Vertex& c = vertices_[sub.indices[i + 2]];
            Vec3 face = Vec3::cross(b.position - a.position, c.position - a.position).normalized();
            if (a.normal.length() < 1e-5f) a.normal = face;
            if (b.normal.length() < 1e-5f) b.normal = face;
            if (c.normal.length() < 1e-5f) c.normal = face;
        }
    }
}

// ---- GPU geometry (added for ShadowMap) -----------------------------------
void Model::UploadToGPU(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool pool, VkQueue queue) {
    if (vertexBuffer_ != VK_NULL_HANDLE || vertices_.empty()) return; // already uploaded / nothing to upload
    gpuDevice_ = device;
    for (auto& material : materials_) uploadMaterialTexture(material.diffuseTexture, device, physicalDevice, pool, queue);

    VkDeviceSize vbSize = sizeof(Vertex) * vertices_.size();
    vkutil::UploadToDeviceLocalBuffer(device, physicalDevice, pool, queue,
        vertices_.data(), vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vertexBuffer_, vertexMemory_);

    // Flatten all sub-meshes into one index buffer, remembering each
    // sub-mesh's {offset,count} so a caller can still draw per-material later.
    std::vector<uint32_t> combinedIndices;
    gpuRanges_.clear();
    for (auto& sub : subMeshes_) {
        GpuSubMeshRange range{ (uint32_t)combinedIndices.size(), (uint32_t)sub.indices.size() };
        combinedIndices.insert(combinedIndices.end(), sub.indices.begin(), sub.indices.end());
        gpuRanges_.push_back(range);
    }

    VkDeviceSize ibSize = sizeof(uint32_t) * combinedIndices.size();
    vkutil::UploadToDeviceLocalBuffer(device, physicalDevice, pool, queue,
        combinedIndices.data(), ibSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        indexBuffer_, indexMemory_);
}

void Model::ReleaseGPU(VkDevice device) {
    for (auto& material : materials_) {
        auto& tex = material.diffuseTexture;
        if (tex.sampler) vkDestroySampler(device, tex.sampler, nullptr);
        if (tex.view) vkDestroyImageView(device, tex.view, nullptr);
        if (tex.image) vkDestroyImage(device, tex.image, nullptr);
        if (tex.memory) vkFreeMemory(device, tex.memory, nullptr);
        tex.sampler = VK_NULL_HANDLE; tex.view = VK_NULL_HANDLE; tex.image = VK_NULL_HANDLE; tex.memory = VK_NULL_HANDLE;
    }
    if (vertexBuffer_) { vkDestroyBuffer(device, vertexBuffer_, nullptr); vertexBuffer_ = VK_NULL_HANDLE; }
    if (vertexMemory_) { vkFreeMemory(device, vertexMemory_, nullptr); vertexMemory_ = VK_NULL_HANDLE; }
    if (indexBuffer_)  { vkDestroyBuffer(device, indexBuffer_, nullptr); indexBuffer_ = VK_NULL_HANDLE; }
    if (indexMemory_)  { vkFreeMemory(device, indexMemory_, nullptr); indexMemory_ = VK_NULL_HANDLE; }
    gpuRanges_.clear();
}

Model::~Model() {
    if (gpuDevice_ != VK_NULL_HANDLE) ReleaseGPU(gpuDevice_);
}
