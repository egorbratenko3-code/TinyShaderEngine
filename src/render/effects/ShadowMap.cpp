#include "ShadowMap.h"
#include "../../core/VulkanUtils.h"
#include "../../scene/Model.h"

#include <fstream>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <cmath>


namespace {
struct PushConsts { Mat4 lightMVP; };
}

Mat4 ShadowMap::BuildLightViewProj(const Scene& scene) const {
    if (scene.GetLights().empty()) return Mat4::identity();
    const auto& light = scene.PrimaryLight();
    Vec3 aim = light.AimDirection();
    if (aim.length() < 1e-4f) aim = Vec3{0, -1, 0};
    Vec3 target = light.position + aim;
    Vec3 up = std::abs(aim.y) > 0.95f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    Mat4 view = Mat4::lookAt(light.position, target, up);
    float fovRad = light.isSpot ? (light.spotConeDegrees * 2.0f * 3.14159265f / 180.0f) : (90.0f * 3.14159265f / 180.0f);
    fovRad = std::clamp(fovRad, 0.1f, 3.1f);
    Mat4 proj = Mat4::perspectiveVulkan(fovRad, 1.0f, 0.1f, 100.0f);
    return Mat4::mul(proj, view);
}


VkShaderModule ShadowMap::loadShaderModule(const std::string& spvPath) {
    std::ifstream file(spvPath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        fprintf(stderr, "ShadowMap: failed to open shader '%s' (compile shaders/shadow.vert -> "
                         "shaders/shadow.vert.spv via glslc, see Makefile)\n", spvPath.c_str());
        return VK_NULL_HANDLE;
    }
    size_t size = (size_t)file.tellg();
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), size);
    file.close();

    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = buffer.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

    VkShaderModule module;
    if (vkCreateShaderModule(device_, &ci, nullptr, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
    return module;
}

bool ShadowMap::Init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool pool, VkQueue queue) {
    device_ = device; physicalDevice_ = physicalDevice; commandPool_ = pool; queue_ = queue;
    if (!createStaticResources()) return false;
    return createResolutionResources();
}

bool ShadowMap::createStaticResources() {
    vertShader_ = loadShaderModule("shaders/shadow.vert.spv");
    if (vertShader_ == VK_NULL_HANDLE) return false;

    // ---- depth-only render pass ----
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat_;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL; // ready for future sampling

    VkAttachmentReference depthRef{ 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency deps[2]{};
    deps[0] = { VK_SUBPASS_EXTERNAL, 0,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_DEPENDENCY_BY_REGION_BIT };
    deps[1] = { 0, VK_SUBPASS_EXTERNAL,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT };

    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &depthAttachment;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;
    if (vkCreateRenderPass(device_, &rpci, nullptr, &renderPass_) != VK_SUCCESS) return false;

    // ---- pipeline layout: one push constant (light MVP) ----
    VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConsts) };
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_) != VK_SUCCESS) return false;

    // ---- pipeline: vertex-only, depth write, no color attachments ----
    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertShader_;
    vertStage.pName = "main";

    VkVertexInputBindingDescription binding{ 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attr{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) };

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE; // avoid peter-panning from front-face-only culling on thin meshes
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    raster.depthBiasEnable = VK_TRUE; // slope-scaled bias set dynamically per-frame from `bias`

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS };
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 3;
    dynState.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 1;
    pci.pStages = &vertStage;
    pci.pVertexInputState = &vertexInput;
    pci.pInputAssemblyState = &inputAssembly;
    pci.pViewportState = &viewportState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &msaa;
    pci.pDepthStencilState = &depthStencil;
    pci.pDynamicState = &dynState;
    pci.layout = pipelineLayout_;
    pci.renderPass = renderPass_;
    pci.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline_) != VK_SUCCESS) return false;

    // ---- comparison sampler, ready for a future shading pass to sample this map ----
    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // outside the map = fully lit, not shadowed
    sci.compareEnable = VK_TRUE;
    sci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL; // standard shadow-comparison sampling
    sci.maxLod = 1.0f;
    return vkCreateSampler(device_, &sci, nullptr, &sampler_) == VK_SUCCESS;
}

bool ShadowMap::createResolutionResources() {
    destroyResolutionResources();

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = depthFormat_;
    ici.extent = { (uint32_t)resolution, (uint32_t)resolution, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &ici, nullptr, &depthImage_) != VK_SUCCESS) return false;

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_, depthImage_, &memReq);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = memReq.size;
    mai.memoryTypeIndex = vkutil::FindMemoryType(physicalDevice_, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &mai, nullptr, &depthMemory_) != VK_SUCCESS) return false;
    vkBindImageMemory(device_, depthImage_, depthMemory_, 0);

    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = depthImage_;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = depthFormat_;
    vci.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(device_, &vci, nullptr, &depthView_) != VK_SUCCESS) return false;

    VkFramebufferCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = renderPass_;
    fci.attachmentCount = 1;
    fci.pAttachments = &depthView_;
    fci.width = resolution;
    fci.height = resolution;
    fci.layers = 1;
    if (vkCreateFramebuffer(device_, &fci, nullptr, &framebuffer_) != VK_SUCCESS) return false;

    builtResolution_ = resolution;
    return true;
}

void ShadowMap::destroyResolutionResources() {
    if (framebuffer_) { vkDestroyFramebuffer(device_, framebuffer_, nullptr); framebuffer_ = VK_NULL_HANDLE; }
    if (depthView_) { vkDestroyImageView(device_, depthView_, nullptr); depthView_ = VK_NULL_HANDLE; }
    if (depthImage_) { vkDestroyImage(device_, depthImage_, nullptr); depthImage_ = VK_NULL_HANDLE; }
    if (depthMemory_) { vkFreeMemory(device_, depthMemory_, nullptr); depthMemory_ = VK_NULL_HANDLE; }
}

bool ShadowMap::Resize(VkDevice device, VkPhysicalDevice physicalDevice) {
    device_ = device; physicalDevice_ = physicalDevice;
    if (resolution == builtResolution_ && depthView_ != VK_NULL_HANDLE) return true;
    vkDeviceWaitIdle(device_);
    return createResolutionResources();
}

void ShadowMap::RenderPass(VkCommandBuffer cmd, const Scene& scene) {
    if (!enabled || !IsAvailable()) return;

    VkClearValue clearDepth{}; clearDepth.depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = renderPass_;
    rpBegin.framebuffer = framebuffer_;
    rpBegin.renderArea.extent = { (uint32_t)resolution, (uint32_t)resolution };
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearDepth;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport vp{ 0, 0, (float)resolution, (float)resolution, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, { (uint32_t)resolution, (uint32_t)resolution } };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdSetDepthBias(cmd, bias * 10000.0f /*constant factor*/, 0.0f, bias * 200.0f /*slope factor*/);

    Mat4 lightVP = BuildLightViewProj(scene);

    auto drawModel = [&](const Model& model) {
        if (!model.IsUploadedToGPU()) return; // Application uploads on load; skip anything not ready
        VkBuffer vb = model.GetVertexBuffer();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
        vkCmdBindIndexBuffer(cmd, model.GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        // Per-object transform (move/rotate) folded in on the CPU — no shader change needed.
        PushConsts pc{ Mat4::mul(lightVP, model.GetModelMatrix()) };
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        for (auto& range : model.GetGpuSubMeshRanges()) {
            vkCmdDrawIndexed(cmd, range.indexCount, 1, range.indexOffset, 0, 0);
        }
    };
    drawModel(scene.GetFloor());
    for (auto& model : scene.GetModels()) drawModel(*model);

    vkCmdEndRenderPass(cmd);
}

void ShadowMap::Shutdown(VkDevice device) {
    destroyResolutionResources();
    if (sampler_) vkDestroySampler(device, sampler_, nullptr);
    if (pipeline_) vkDestroyPipeline(device, pipeline_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    if (renderPass_) vkDestroyRenderPass(device, renderPass_, nullptr);
    if (vertShader_) vkDestroyShaderModule(device, vertShader_, nullptr);
    sampler_ = VK_NULL_HANDLE; pipeline_ = VK_NULL_HANDLE; pipelineLayout_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE; vertShader_ = VK_NULL_HANDLE;
}
