#include "SceneRenderer.h"
#include "../core/VulkanUtils.h"
#include "../scene/Model.h"

#include <fstream>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace {
struct GPULight {
    float posType[4];    // xyz = pos, w = type
    float colorInt[4];   // rgb = color, a = intensity
    float dirCone[4];    // xyz = aim dir, w = cos(halfAngle)
    float spotParams[4]; // x = isSpot, y = cosInner, z = range, w = unused
};

struct SceneUBO {
    Mat4 viewProj;
    Mat4 lightViewProj;
    float cameraPos[4];
    float shadowParams[4]; // x=bias, y=pcfRadius, z=shadowEnabled, w=unused
    float lightCount[4];   // x = count
    GPULight lights[16];
};
struct ModelPC { Mat4 modelMatrix; float materialColor[4]; };
}


static VkShaderModule LoadShader(VkDevice device, const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        fprintf(stderr, "SceneRenderer: failed to open shader '%s'\n", path.c_str());
        return VK_NULL_HANDLE;
    }
    size_t size = (size_t)file.tellg();
    std::vector<char> buf(size);
    file.seekg(0); file.read(buf.data(), size); file.close();
    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = buf.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(buf.data());
    VkShaderModule mod;
    return vkCreateShaderModule(device, &ci, nullptr, &mod) == VK_SUCCESS ? mod : VK_NULL_HANDLE;
}

bool SceneRenderer::Init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool pool, VkQueue queue,
                          uint32_t width, uint32_t height, VkFormat colorFormat) {
    device_ = device; physicalDevice_ = physicalDevice;
    width_ = width; height_ = height; colorFormat_ = colorFormat;

    vertShader_ = LoadShader(device_, "shaders/scene.vert.spv");
    fragShader_ = LoadShader(device_, "shaders/scene.frag.spv");
    if (!vertShader_ || !fragShader_) return false;

    vkutil::CreateColorAttachment(device_, physicalDevice_, width_, height_, colorFormat_, colorImage_, colorMemory_, colorView_);
    vkutil::CreateColorAttachment(device_, physicalDevice_, width_, height_, VK_FORMAT_R32_SFLOAT, linearDepthImage_, linearDepthMemory_, linearDepthView_);

    // ---- true depth buffer (Z-test only; not sampled elsewhere) ----
    VkImageCreateInfo dici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    dici.imageType = VK_IMAGE_TYPE_2D; dici.format = depthFormat_;
    dici.extent = { width_, height_, 1 }; dici.mipLevels = 1; dici.arrayLayers = 1;
    dici.samples = VK_SAMPLE_COUNT_1_BIT; dici.tiling = VK_IMAGE_TILING_OPTIMAL;
    dici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    vkCreateImage(device_, &dici, nullptr, &depthImage_);
    VkMemoryRequirements dmr; vkGetImageMemoryRequirements(device_, depthImage_, &dmr);
    VkMemoryAllocateInfo dmai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    dmai.allocationSize = dmr.size;
    dmai.memoryTypeIndex = vkutil::FindMemoryType(physicalDevice_, dmr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &dmai, nullptr, &depthMemory_);
    vkBindImageMemory(device_, depthImage_, depthMemory_, 0);
    VkImageViewCreateInfo dvci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    dvci.image = depthImage_; dvci.viewType = VK_IMAGE_VIEW_TYPE_2D; dvci.format = depthFormat_;
    dvci.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    vkCreateImageView(device_, &dvci, nullptr, &depthView_);

    // ---- render pass: color(RGBA16F) + linearDepth(R32F) + depth(D32) ----
    VkAttachmentDescription atts[3]{};
    atts[0] = { 0, colorFormat_, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    atts[1] = { 0, VK_FORMAT_R32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    atts[2] = { 0, depthFormat_, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkAttachmentReference colorRefs[2] = { {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL} };
    VkAttachmentReference depthRef{ 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 2; subpass.pColorAttachments = colorRefs;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpci.attachmentCount = 3; rpci.pAttachments = atts;
    rpci.subpassCount = 1; rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1; rpci.pDependencies = &dep;
    if (vkCreateRenderPass(device_, &rpci, nullptr, &renderPass_) != VK_SUCCESS) return false;

    VkImageView fbAtts[3] = { colorView_, linearDepthView_, depthView_ };
    VkFramebufferCreateInfo fci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fci.renderPass = renderPass_; fci.attachmentCount = 3; fci.pAttachments = fbAtts;
    fci.width = width_; fci.height = height_; fci.layers = 1;
    if (vkCreateFramebuffer(device_, &fci, nullptr, &framebuffer_) != VK_SUCCESS) return false;

    // ---- descriptor set layout: SceneUBO (binding 0) + shadow comparison sampler (binding 1) ----
    VkDescriptorSetLayoutBinding bindings[2] = {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    };
    VkDescriptorSetLayoutCreateInfo dslci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dslci.bindingCount = 2; dslci.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device_, &dslci, nullptr, &setLayout_) != VK_SUCCESS) return false;

    VkDescriptorSetLayoutBinding textureBinding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo textureLayoutCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    textureLayoutCI.bindingCount = 1; textureLayoutCI.pBindings = &textureBinding;
    if (vkCreateDescriptorSetLayout(device_, &textureLayoutCI, nullptr, &textureSetLayout_) != VK_SUCCESS) return false;

    VkDescriptorPoolSize poolSizes[2] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight + 1024 },
    };
    VkDescriptorPoolCreateInfo dpci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpci.maxSets = kFramesInFlight + 1024; dpci.poolSizeCount = 2; dpci.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device_, &dpci, nullptr, &descPool_) != VK_SUCCESS) return false;

    VkDescriptorSetLayout layouts[kFramesInFlight]; for (auto& l : layouts) l = setLayout_;
    VkDescriptorSetAllocateInfo dsai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool = descPool_; dsai.descriptorSetCount = kFramesInFlight; dsai.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(device_, &dsai, descSets_) != VK_SUCCESS) return false;

    for (int i = 0; i < kFramesInFlight; i++) {
        vkutil::CreateBuffer(device_, physicalDevice_, sizeof(SceneUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ubo_[i], uboMemory_[i]);
        vkMapMemory(device_, uboMemory_[i], 0, sizeof(SceneUBO), 0, &uboMapped_[i]);
    }

    VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ModelPC) };
    VkDescriptorSetLayout pipelineLayouts[] = { setLayout_, textureSetLayout_ };
    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 2; plci.pSetLayouts = pipelineLayouts;
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_) != VK_SUCCESS) return false;

    // ---- pipeline ----
    VkPipelineShaderStageCreateInfo vertStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT; vertStage.module = vertShader_; vertStage.pName = "main";
    VkPipelineShaderStageCreateInfo fragStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fragStage.module = fragShader_; fragStage.pName = "main";
    VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

    VkVertexInputBindingDescription binding{ 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attrs[3] = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) },
        { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, u) },
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertexInput.vertexBindingDescriptionCount = 1; vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3; vertexInput.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1; viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencil.depthTestEnable = VK_TRUE; depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAtts[2]{};
    for (auto& b : blendAtts) b.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    blend.attachmentCount = 2; blend.pAttachments = blendAtts;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynState.dynamicStateCount = 2; dynState.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pci.stageCount = 2; pci.pStages = stages;
    pci.pVertexInputState = &vertexInput;
    pci.pInputAssemblyState = &inputAssembly;
    pci.pViewportState = &viewportState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &msaa;
    pci.pDepthStencilState = &depthStencil;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dynState;
    pci.layout = pipelineLayout_;
    pci.renderPass = renderPass_;
    pci.subpass = 0;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline_) != VK_SUCCESS) return false;
    return createWhiteTexture(pool, queue);
}

bool SceneRenderer::createWhiteTexture(VkCommandPool pool, VkQueue queue) {
    uint32_t pixel = 0xffffffffu;
    VkBuffer staging{}; VkDeviceMemory stagingMemory{};
    vkutil::CreateBuffer(device_, physicalDevice_, sizeof(pixel), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMemory);
    void* data{}; vkMapMemory(device_, stagingMemory, 0, sizeof(pixel), 0, &data); std::memcpy(data, &pixel, sizeof(pixel)); vkUnmapMemory(device_, stagingMemory);
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ici.imageType=VK_IMAGE_TYPE_2D; ici.format=VK_FORMAT_R8G8B8A8_SRGB; ici.extent={1,1,1}; ici.mipLevels=1; ici.arrayLayers=1; ici.samples=VK_SAMPLE_COUNT_1_BIT; ici.tiling=VK_IMAGE_TILING_OPTIMAL; ici.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    if (vkCreateImage(device_, &ici, nullptr, &whiteTextureImage_) != VK_SUCCESS) return false;
    VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device_, whiteTextureImage_, &req); VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize=req.size; mai.memoryTypeIndex=vkutil::FindMemoryType(physicalDevice_,req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &mai, nullptr, &whiteTextureMemory_); vkBindImageMemory(device_, whiteTextureImage_, whiteTextureMemory_, 0);
    VkCommandBuffer cmd=vkutil::BeginSingleTimeCommands(device_,pool); VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.image=whiteTextureImage_;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b); VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1,1,1};vkCmdCopyBufferToImage(cmd,staging,whiteTextureImage_,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);vkutil::EndSingleTimeCommands(device_,pool,queue,cmd);vkDestroyBuffer(device_,staging,nullptr);vkFreeMemory(device_,stagingMemory,nullptr);
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vci.image=whiteTextureImage_;vci.viewType=VK_IMAGE_VIEW_TYPE_2D;vci.format=VK_FORMAT_R8G8B8A8_SRGB;vci.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkCreateImageView(device_,&vci,nullptr,&whiteTextureView_);VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sci.magFilter=sci.minFilter=VK_FILTER_LINEAR;sci.addressModeU=sci.addressModeV=sci.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;sci.maxLod=1;return vkCreateSampler(device_,&sci,nullptr,&whiteTextureSampler_)==VK_SUCCESS;
}

VkDescriptorSet SceneRenderer::textureSetFor(const MaterialTexture* texture) {
    const MaterialTexture* key = texture && texture->view && texture->sampler ? texture : nullptr;
    auto it = textureSets_.find(key); if (it != textureSets_.end()) return it->second;
    VkDescriptorSet set{}; VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; ai.descriptorPool=descPool_;ai.descriptorSetCount=1;ai.pSetLayouts=&textureSetLayout_;
    if (vkAllocateDescriptorSets(device_, &ai, &set) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkDescriptorImageInfo image{key ? key->sampler : whiteTextureSampler_, key ? key->view : whiteTextureView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set;write.dstBinding=0;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&image;vkUpdateDescriptorSets(device_,1,&write,0,nullptr);textureSets_[key]=set;return set;
}

SceneRenderer::UV SceneRenderer::BuildLightScreenUV(const Scene& scene, float aspect) const {
    const auto& cam = scene.ActiveCamera();
    Mat4 view = Mat4::lookAt(cam.position, cam.target, Vec3{0, 1, 0});
    float fovRad = cam.fovDegrees * 3.14159265f / 180.0f;
    Mat4 proj = Mat4::perspectiveVulkan(fovRad, aspect, 0.05f, 500.0f);
    Mat4 vp = Mat4::mul(proj, view);

    Vec3 lightPos = scene.GetLights().empty() ? Vec3{0, 2, 0} : scene.PrimaryLight().position;
    const SceneNode* sel = scene.SelectedNode();
    for (const auto& l : scene.GetLights()) {
        if (l.get() == sel) {
            lightPos = l->position;
            break;
        }
    }

    float clip[4]; vp.transformPoint(lightPos, clip);
    if (clip[3] <= 0.0001f) return { 0.5f, 0.5f, false };
    return { (clip[0] / clip[3]) * 0.5f + 0.5f, 1.0f - ((clip[1] / clip[3]) * 0.5f + 0.5f), true };
}

void SceneRenderer::RecordPass(VkCommandBuffer cmd, const Scene& scene, const ShadowMap& shadowMap, float aspect) {
    // Guard against Init() having failed partway (missing shaders/scene.*.spv,
    // a Vulkan object failing to create, etc). Without this, a failed Init
    // still leaves this called every frame from Application::renderPipeline,
    // and uboMapped_[slot] being null turns the memcpy below into a crash.
    if (!IsAvailable()) return;

    // ---- update this frame's UBO + descriptor set ----
    int slot = frameSlot_;
    frameSlot_ = 1 - frameSlot_;

    const auto& cam = scene.ActiveCamera();
    SceneUBO ubo{};
    ubo.viewProj = Mat4::mul(Mat4::perspectiveVulkan(cam.fovDegrees * 3.14159265f / 180.0f, aspect, 0.05f, 500.0f),
                              Mat4::lookAt(cam.position, cam.target, Vec3{0,1,0}));
    ubo.lightViewProj = shadowMap.BuildLightViewProj(scene);
    ubo.cameraPos[0] = cam.position.x; ubo.cameraPos[1] = cam.position.y; ubo.cameraPos[2] = cam.position.z; ubo.cameraPos[3] = 1.0f;
    ubo.shadowParams[0] = shadowMap.bias; ubo.shadowParams[1] = shadowMap.pcfRadius;
    ubo.shadowParams[2] = (shadowMap.enabled && shadowMap.IsAvailable()) ? 1.0f : 0.0f; ubo.shadowParams[3] = 0.0f;

    const auto& sceneLights = scene.GetLights();
    uint32_t count = std::min((uint32_t)sceneLights.size(), 16u);
    ubo.lightCount[0] = (float)count;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& l = *sceneLights[i];
        ubo.lights[i].posType[0] = l.position.x;
        ubo.lights[i].posType[1] = l.position.y;
        ubo.lights[i].posType[2] = l.position.z;
        ubo.lights[i].posType[3] = l.isSpot ? 1.0f : 0.0f;

        ubo.lights[i].colorInt[0] = l.colorTint.x;
        ubo.lights[i].colorInt[1] = l.colorTint.y;
        ubo.lights[i].colorInt[2] = l.colorTint.z;
        ubo.lights[i].colorInt[3] = l.intensity;

        Vec3 aim = l.AimDirection();
        ubo.lights[i].dirCone[0] = aim.x;
        ubo.lights[i].dirCone[1] = aim.y;
        ubo.lights[i].dirCone[2] = aim.z;
        ubo.lights[i].dirCone[3] = std::cos(l.spotConeDegrees * 3.14159265f / 180.0f);

        ubo.lights[i].spotParams[0] = l.isSpot ? 1.0f : 0.0f;
        ubo.lights[i].spotParams[1] = 0.0f;
        ubo.lights[i].spotParams[2] = 0.0f;
        ubo.lights[i].spotParams[3] = 0.0f;
    }
    std::memcpy(uboMapped_[slot], &ubo, sizeof(ubo));


    VkDescriptorBufferInfo bufInfo{ ubo_[slot], 0, sizeof(SceneUBO) };
    // Fall back to the shadow sampler's own (always-valid) view/sampler even when disabled, since the
    // descriptor must reference a real image; the shader's shadowParams.z flag decides whether to use it.
    VkImageView shadowView = shadowMap.IsAvailable() ? shadowMap.GetDepthView() : VK_NULL_HANDLE;
    VkSampler shadowSampler = shadowMap.IsAvailable() ? shadowMap.GetSampler() : VK_NULL_HANDLE;

    VkWriteDescriptorSet writes[2]{};
    writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descSets_[slot], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bufInfo, nullptr };
    VkDescriptorImageInfo imgInfo{ shadowSampler, shadowView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
    writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descSets_[slot], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgInfo, nullptr, nullptr };
    uint32_t writeCount = shadowView != VK_NULL_HANDLE ? 2 : 1; // skip binding 1 until ShadowMap has produced its first depth image
    vkUpdateDescriptorSets(device_, writeCount, writes, 0, nullptr);
    if (writeCount < 2) return; // descriptor set incomplete (ShadowMap not yet initialized) — skip this frame's scene pass safely

    VkClearValue clears[3];
    clears[0].color = { {0.05f, 0.05f, 0.07f, 1.0f} };
    clears[1].color = { {1000.0f, 0, 0, 0} }; // "far" sentinel for linear depth — GodRays treats this as unoccluded sky
    clears[2].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpBegin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpBegin.renderPass = renderPass_; rpBegin.framebuffer = framebuffer_;
    rpBegin.renderArea.extent = { width_, height_ };
    rpBegin.clearValueCount = 3; rpBegin.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkViewport vp{ 0, 0, (float)width_, (float)height_, 0.0f, 1.0f };
    VkRect2D sc{ {0,0}, {width_, height_} };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descSets_[slot], 0, nullptr);

    auto drawModel = [&](const Model& model) {
        if (!model.IsUploadedToGPU()) return;
        VkBuffer vb = model.GetVertexBuffer();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
        vkCmdBindIndexBuffer(cmd, model.GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        Mat4 modelMatrix = model.GetModelMatrix(); // per-object move/rotate transform

        for (auto& range : model.GetGpuSubMeshRanges()) {
            // Find this range's submesh index to look up its material color (ranges are 1:1 with sub-meshes, in order).
            size_t subIndex = &range - &model.GetGpuSubMeshRanges()[0];
            const auto& subMeshes = model.GetSubMeshes();
            Vec3 color(0.8f, 0.8f, 0.8f);
            if (subIndex < subMeshes.size() && subMeshes[subIndex].materialIndex >= 0) {
                const auto& mats = model.GetMaterials();
                int mi = subMeshes[subIndex].materialIndex;
                if (mi >= 0 && (size_t)mi < mats.size()) color = mats[mi].diffuseColor;
            }
            const MaterialTexture* texture = nullptr;
            if (subIndex < subMeshes.size() && subMeshes[subIndex].materialIndex >= 0) {
                const auto& mats = model.GetMaterials(); int mi = subMeshes[subIndex].materialIndex;
                if (mi >= 0 && (size_t)mi < mats.size()) texture = &mats[mi].diffuseTexture;
            }
            VkDescriptorSet textureSet = textureSetFor(texture);
            if (textureSet == VK_NULL_HANDLE) continue;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 1, 1, &textureSet, 0, nullptr);
            ModelPC pc{ modelMatrix, { color.x, color.y, color.z, 1.0f } };
            vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cmd, range.indexCount, 1, range.indexOffset, 0, 0);
        }
    };
    drawModel(scene.GetFloor());
    for (auto& model : scene.GetModels()) drawModel(*model);

    vkCmdEndRenderPass(cmd);
}

void SceneRenderer::Shutdown(VkDevice device) {
    for (int i = 0; i < kFramesInFlight; i++) {
        if (uboMapped_[i]) vkUnmapMemory(device, uboMemory_[i]);
        if (ubo_[i]) vkDestroyBuffer(device, ubo_[i], nullptr);
        if (uboMemory_[i]) vkFreeMemory(device, uboMemory_[i], nullptr);
    }
    if (framebuffer_) vkDestroyFramebuffer(device, framebuffer_, nullptr);
    if (colorView_) vkDestroyImageView(device, colorView_, nullptr);
    if (colorImage_) vkDestroyImage(device, colorImage_, nullptr);
    if (colorMemory_) vkFreeMemory(device, colorMemory_, nullptr);
    if (linearDepthView_) vkDestroyImageView(device, linearDepthView_, nullptr);
    if (linearDepthImage_) vkDestroyImage(device, linearDepthImage_, nullptr);
    if (linearDepthMemory_) vkFreeMemory(device, linearDepthMemory_, nullptr);
    if (depthView_) vkDestroyImageView(device, depthView_, nullptr);
    if (depthImage_) vkDestroyImage(device, depthImage_, nullptr);
    if (depthMemory_) vkFreeMemory(device, depthMemory_, nullptr);
    if (pipeline_) vkDestroyPipeline(device, pipeline_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    if (setLayout_) vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
    if (textureSetLayout_) vkDestroyDescriptorSetLayout(device, textureSetLayout_, nullptr);
    if (descPool_) vkDestroyDescriptorPool(device, descPool_, nullptr);
    if (renderPass_) vkDestroyRenderPass(device, renderPass_, nullptr);
    if (vertShader_) vkDestroyShaderModule(device, vertShader_, nullptr);
    if (fragShader_) vkDestroyShaderModule(device, fragShader_, nullptr);
    if (whiteTextureSampler_) vkDestroySampler(device, whiteTextureSampler_, nullptr);
    if (whiteTextureView_) vkDestroyImageView(device, whiteTextureView_, nullptr);
    if (whiteTextureImage_) vkDestroyImage(device, whiteTextureImage_, nullptr);
    if (whiteTextureMemory_) vkFreeMemory(device, whiteTextureMemory_, nullptr);
}
