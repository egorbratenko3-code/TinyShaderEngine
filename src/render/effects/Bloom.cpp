#include "Bloom.h"
#include "../../core/VulkanUtils.h"

#include <fstream>
#include <vector>
#include <cstdio>
#include <algorithm>

namespace {
struct DownsamplePC { float texelSize[2]; float threshold; int32_t prefilter; };
struct UpsamplePC   { float texelSize[2]; float radius; float _pad; };
}

VkShaderModule Bloom::loadShader(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        fprintf(stderr, "Bloom: failed to open shader '%s' (compile shaders/*.vert|frag -> *.spv via glslc)\n", path.c_str());
        return VK_NULL_HANDLE;
    }
    size_t size = (size_t)file.tellg();
    std::vector<char> buf(size);
    file.seekg(0); file.read(buf.data(), size); file.close();

    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = buf.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(buf.data());
    VkShaderModule mod;
    return vkCreateShaderModule(device_, &ci, nullptr, &mod) == VK_SUCCESS ? mod : VK_NULL_HANDLE;
}

bool Bloom::Init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool pool, VkQueue queue,
                  uint32_t baseWidth, uint32_t baseHeight, VkFormat colorFormat) {
    device_ = device; physicalDevice_ = physicalDevice;
    baseWidth_ = baseWidth; baseHeight_ = baseHeight; colorFormat_ = colorFormat;
    mipLevels = std::clamp(mipLevels, 2, kMaxMips);

    if (!createStaticResources()) return false;
    if (!createMipChain()) return false;
    // Mip 0 is the externally-sampled result (see GetResultView). Pre-clear
    // it to black so Composite can safely sample it even before Bloom has
    // ever run a real pass (e.g. while the effect is toggled off).
    vkutil::ClearColorToBlackAndTransition(device_, pool, queue, mips_[0].image);
    return true;
}

bool Bloom::createStaticResources() {
    vertShader_ = loadShader("shaders/fullscreen.vert.spv");
    downsampleFrag_ = loadShader("shaders/bloom_downsample.frag.spv");
    upsampleFrag_ = loadShader("shaders/bloom_upsample.frag.spv");
    if (!vertShader_ || !downsampleFrag_ || !upsampleFrag_) return false;

    // ---- two render pass variants over the same attachment format: one
    // that clears (downsample targets), one that loads+additive-blends
    // (upsample targets). Both are "compatible" (same format/sample count)
    // so a mip's image view can be framebuffered against either. ----
    auto makeRenderPass = [&](VkAttachmentLoadOp loadOp, VkImageLayout initialLayout, VkRenderPass& out) {
        VkAttachmentDescription att{};
        att.format = colorFormat_;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = loadOp;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = initialLayout;
        att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &ref;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 1; ci.pAttachments = &att;
        ci.subpassCount = 1; ci.pSubpasses = &subpass;
        ci.dependencyCount = 1; ci.pDependencies = &dep;
        return vkCreateRenderPass(device_, &ci, nullptr, &out) == VK_SUCCESS;
    };
    if (!makeRenderPass(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, renderPassClear_)) return false;
    if (!makeRenderPass(VK_ATTACHMENT_LOAD_OP_LOAD, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, renderPassAdditive_)) return false;

    // ---- descriptor set layout: one combined-image-sampler ----
    VkDescriptorSetLayoutBinding binding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo dslci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dslci.bindingCount = 1; dslci.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device_, &dslci, nullptr, &setLayout_) != VK_SUCCESS) return false;

    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxMips + 2 };
    VkDescriptorPoolCreateInfo dpci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpci.maxSets = kMaxMips + 2; dpci.poolSizeCount = 1; dpci.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_, &dpci, nullptr, &descPool_) != VK_SUCCESS) return false;

    VkPushConstantRange pcRange{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DownsamplePC) }; // both PCs are 16 bytes
    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 1; plci.pSetLayouts = &setLayout_;
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_) != VK_SUCCESS) return false;

    VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sci.magFilter = VK_FILTER_LINEAR; sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 1.0f;
    if (vkCreateSampler(device_, &sci, nullptr, &sampler_) != VK_SUCCESS) return false;

    // ---- two pipelines sharing layout/vertex-shader, differing in fragment
    // shader, blend state, and render pass ----
    VkPipelineShaderStageCreateInfo vertStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT; vertStage.module = vertShader_; vertStage.pName = "main";

    VkPipelineVertexInputStateCreateInfo emptyVertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1; viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynState.dynamicStateCount = 2; dynState.pDynamicStates = dynStates;

    VkPipelineColorBlendAttachmentState blendOpaque{};
    blendOpaque.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendOpaque.blendEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAdditive = blendOpaque;
    blendAdditive.blendEnable = VK_TRUE;
    blendAdditive.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAdditive.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAdditive.colorBlendOp = VK_BLEND_OP_ADD;
    blendAdditive.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAdditive.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAdditive.alphaBlendOp = VK_BLEND_OP_ADD;

    auto makePipeline = [&](VkShaderModule frag, VkPipelineColorBlendAttachmentState blendState,
                             VkRenderPass rp, VkPipeline& out) {
        VkPipelineShaderStageCreateInfo fragStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fragStage.module = frag; fragStage.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

        VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1; blend.pAttachments = &blendState;

        VkGraphicsPipelineCreateInfo pci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pci.stageCount = 2; pci.pStages = stages;
        pci.pVertexInputState = &emptyVertexInput;
        pci.pInputAssemblyState = &inputAssembly;
        pci.pViewportState = &viewportState;
        pci.pRasterizationState = &raster;
        pci.pMultisampleState = &msaa;
        pci.pColorBlendState = &blend;
        pci.pDynamicState = &dynState;
        pci.layout = pipelineLayout_;
        pci.renderPass = rp;
        pci.subpass = 0;
        return vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &out) == VK_SUCCESS;
    };
    if (!makePipeline(downsampleFrag_, blendOpaque, renderPassClear_, downsamplePipeline_)) return false;
    if (!makePipeline(upsampleFrag_, blendAdditive, renderPassAdditive_, upsamplePipeline_)) return false;

    // ---- external-input descriptor sets, double-buffered so updating one
    // frame's set can't race the previous frame's still-in-flight GPU read ----
    VkDescriptorSetLayout layouts2[2] = { setLayout_, setLayout_ };
    VkDescriptorSetAllocateInfo dsai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool = descPool_; dsai.descriptorSetCount = 2; dsai.pSetLayouts = layouts2;
    return vkAllocateDescriptorSets(device_, &dsai, externalInputSets_) == VK_SUCCESS;
}

void Bloom::writeDescriptor(VkDescriptorSet set, VkImageView view) {
    VkDescriptorImageInfo imgInfo{ sampler_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = set; write.dstBinding = 0; write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

bool Bloom::createMipChain() {
    destroyMipChain();
    mips_.resize(kMaxMips);

    uint32_t w = baseWidth_, h = baseHeight_;
    for (int i = 0; i < kMaxMips; i++) {
        w = std::max(1u, w); h = std::max(1u, h);
        MipTarget& mip = mips_[i];
        mip.width = w; mip.height = h;
        vkutil::CreateColorAttachment(device_, physicalDevice_, w, h, colorFormat_, mip.image, mip.memory, mip.view);

        VkFramebufferCreateInfo fciClear{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fciClear.renderPass = renderPassClear_; fciClear.attachmentCount = 1; fciClear.pAttachments = &mip.view;
        fciClear.width = w; fciClear.height = h; fciClear.layers = 1;
        if (vkCreateFramebuffer(device_, &fciClear, nullptr, &mip.fbClear) != VK_SUCCESS) return false;

        VkFramebufferCreateInfo fciLoad = fciClear;
        fciLoad.renderPass = renderPassAdditive_;
        if (vkCreateFramebuffer(device_, &fciLoad, nullptr, &mip.fbLoad) != VK_SUCCESS) return false;

        VkDescriptorSetAllocateInfo dsai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        dsai.descriptorPool = descPool_; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &setLayout_;
        if (vkAllocateDescriptorSets(device_, &dsai, &mip.readSet) != VK_SUCCESS) return false;
        writeDescriptor(mip.readSet, mip.view); // static: this mip's own view never changes underneath it

        w /= 2; h /= 2;
    }
    return true;
}

void Bloom::destroyMipChain() {
    for (auto& mip : mips_) {
        if (mip.fbClear) vkDestroyFramebuffer(device_, mip.fbClear, nullptr);
        if (mip.fbLoad) vkDestroyFramebuffer(device_, mip.fbLoad, nullptr);
        if (mip.view) vkDestroyImageView(device_, mip.view, nullptr);
        if (mip.image) vkDestroyImage(device_, mip.image, nullptr);
        if (mip.memory) vkFreeMemory(device_, mip.memory, nullptr);
        // readSet freed implicitly when descPool_ is destroyed in Shutdown()
    }
    mips_.clear();
}

void Bloom::RecordPass(VkCommandBuffer cmd, VkImageView inputColorView) {
    // mips_.empty() alone isn't sufficient: createMipChain() resizes mips_
    // up front, before doing any real work, so a partial failure there still
    // leaves mips_ non-empty but full of null handles. Also check a pipeline
    // handle to catch that case (currently masked by `enabled` defaulting to
    // false, but not once someone enables Bloom after a silent Init failure).
    if (!enabled || mips_.empty() || downsamplePipeline_ == VK_NULL_HANDLE) return;
    int levels = std::clamp(mipLevels, 2, kMaxMips);

    auto drawFullscreen = [&](VkPipeline pipeline, VkFramebuffer fb, VkRenderPass rp,
                               uint32_t w, uint32_t h, VkDescriptorSet srcSet, const void* pushData, uint32_t pushSize) {
        VkClearValue clear{}; clear.color = { {0,0,0,1} };
        VkRenderPassBeginInfo rpBegin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rpBegin.renderPass = rp; rpBegin.framebuffer = fb;
        rpBegin.renderArea.extent = { w, h };
        rpBegin.clearValueCount = 1; rpBegin.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkViewport vp{ 0, 0, (float)w, (float)h, 0.0f, 1.0f };
        VkRect2D sc{ {0,0}, {w,h} };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &srcSet, 0, nullptr);
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, pushData);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    };

    // ---- downsample: external input -> mip0 -> mip1 -> ... -> mip[levels-1] ----
    VkDescriptorSet extSet = externalInputSets_[frameSlot_];
    writeDescriptor(extSet, inputColorView);
    frameSlot_ = 1 - frameSlot_;

    DownsamplePC pc0{ {1.0f / baseWidth_, 1.0f / baseHeight_}, threshold, 1 };
    drawFullscreen(downsamplePipeline_, mips_[0].fbClear, renderPassClear_, mips_[0].width, mips_[0].height,
                   extSet, &pc0, sizeof(pc0));

    for (int i = 1; i < levels; i++) {
        DownsamplePC pc{ {1.0f / mips_[i-1].width, 1.0f / mips_[i-1].height}, threshold, 0 };
        drawFullscreen(downsamplePipeline_, mips_[i].fbClear, renderPassClear_, mips_[i].width, mips_[i].height,
                       mips_[i-1].readSet, &pc, sizeof(pc));
    }

    // ---- upsample: mip[levels-1] -> ... -> mip1 -> mip0, additively blended ----
    for (int i = levels - 2; i >= 0; i--) {
        UpsamplePC pc{ {1.0f / mips_[i+1].width, 1.0f / mips_[i+1].height}, std::max(intensity, 0.01f) * 2.0f, 0.0f };
        drawFullscreen(upsamplePipeline_, mips_[i].fbLoad, renderPassAdditive_, mips_[i].width, mips_[i].height,
                       mips_[i+1].readSet, &pc, sizeof(pc));
    }
}

void Bloom::Shutdown(VkDevice device) {
    destroyMipChain();
    if (downsamplePipeline_) vkDestroyPipeline(device, downsamplePipeline_, nullptr);
    if (upsamplePipeline_) vkDestroyPipeline(device, upsamplePipeline_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    if (setLayout_) vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
    if (descPool_) vkDestroyDescriptorPool(device, descPool_, nullptr);
    if (sampler_) vkDestroySampler(device, sampler_, nullptr);
    if (renderPassClear_) vkDestroyRenderPass(device, renderPassClear_, nullptr);
    if (renderPassAdditive_) vkDestroyRenderPass(device, renderPassAdditive_, nullptr);
    if (vertShader_) vkDestroyShaderModule(device, vertShader_, nullptr);
    if (downsampleFrag_) vkDestroyShaderModule(device, downsampleFrag_, nullptr);
    if (upsampleFrag_) vkDestroyShaderModule(device, upsampleFrag_, nullptr);
    downsamplePipeline_ = upsamplePipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE; setLayout_ = VK_NULL_HANDLE; descPool_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE; renderPassClear_ = renderPassAdditive_ = VK_NULL_HANDLE;
    vertShader_ = downsampleFrag_ = upsampleFrag_ = VK_NULL_HANDLE;
}
