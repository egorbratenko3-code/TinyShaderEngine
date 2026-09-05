#include "ColorCorrect.h"
#include "../../core/VulkanUtils.h"

#include <fstream>
#include <vector>
#include <cstdio>

namespace { struct PushConsts { float exposure, contrast, saturation, _pad, filterR, filterG, filterB; }; }

VkShaderModule ColorCorrect::loadShader(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) { fprintf(stderr, "ColorCorrect: failed to open shader '%s'\n", path.c_str()); return VK_NULL_HANDLE; }
    size_t size = (size_t)file.tellg();
    std::vector<char> buf(size);
    file.seekg(0); file.read(buf.data(), size); file.close();
    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = buf.size(); ci.pCode = reinterpret_cast<const uint32_t*>(buf.data());
    VkShaderModule mod;
    return vkCreateShaderModule(device_, &ci, nullptr, &mod) == VK_SUCCESS ? mod : VK_NULL_HANDLE;
}

bool ColorCorrect::Init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool /*pool*/, VkQueue /*queue*/,
                         uint32_t width, uint32_t height, VkFormat colorFormat) {
    device_ = device; physicalDevice_ = physicalDevice; width_ = width; height_ = height; colorFormat_ = colorFormat;

    vertShader_ = loadShader("shaders/fullscreen.vert.spv");
    fragShader_ = loadShader("shaders/colorcorrect.frag.spv");
    if (!vertShader_ || !fragShader_) return false;

    vkutil::CreateColorAttachment(device_, physicalDevice_, width_, height_, colorFormat_, outputImage_, outputMemory_, outputView_);

    VkAttachmentDescription att{};
    att.format = colorFormat_; att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &ref;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpci.attachmentCount = 1; rpci.pAttachments = &att; rpci.subpassCount = 1; rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1; rpci.pDependencies = &dep;
    if (vkCreateRenderPass(device_, &rpci, nullptr, &renderPass_) != VK_SUCCESS) return false;

    VkFramebufferCreateInfo fci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fci.renderPass = renderPass_; fci.attachmentCount = 1; fci.pAttachments = &outputView_;
    fci.width = width_; fci.height = height_; fci.layers = 1;
    if (vkCreateFramebuffer(device_, &fci, nullptr, &framebuffer_) != VK_SUCCESS) return false;

    VkDescriptorSetLayoutBinding binding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo dslci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dslci.bindingCount = 1; dslci.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device_, &dslci, nullptr, &setLayout_) != VK_SUCCESS) return false;

    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 };
    VkDescriptorPoolCreateInfo dpci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpci.maxSets = 2; dpci.poolSizeCount = 1; dpci.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_, &dpci, nullptr, &descPool_) != VK_SUCCESS) return false;

    VkDescriptorSetLayout layouts2[2] = { setLayout_, setLayout_ };
    VkDescriptorSetAllocateInfo dsai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool = descPool_; dsai.descriptorSetCount = 2; dsai.pSetLayouts = layouts2;
    if (vkAllocateDescriptorSets(device_, &dsai, inputSets_) != VK_SUCCESS) return false;

    VkPushConstantRange pcRange{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConsts) };
    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 1; plci.pSetLayouts = &setLayout_;
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_) != VK_SUCCESS) return false;

    VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sci.magFilter = VK_FILTER_LINEAR; sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 1.0f;
    if (vkCreateSampler(device_, &sci, nullptr, &sampler_) != VK_SUCCESS) return false;

    VkPipelineShaderStageCreateInfo vertStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT; vertStage.module = vertShader_; vertStage.pName = "main";
    VkPipelineShaderStageCreateInfo fragStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fragStage.module = fragShader_; fragStage.pName = "main";
    VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

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
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blendState{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    blendState.attachmentCount = 1; blendState.pAttachments = &blend;
    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynState.dynamicStateCount = 2; dynState.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pci.stageCount = 2; pci.pStages = stages;
    pci.pVertexInputState = &emptyVertexInput; pci.pInputAssemblyState = &inputAssembly;
    pci.pViewportState = &viewportState; pci.pRasterizationState = &raster;
    pci.pMultisampleState = &msaa; pci.pColorBlendState = &blendState; pci.pDynamicState = &dynState;
    pci.layout = pipelineLayout_; pci.renderPass = renderPass_; pci.subpass = 0;
    return vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline_) == VK_SUCCESS;
}

void ColorCorrect::RecordPass(VkCommandBuffer cmd, VkImageView inputColorView) {
    if (!enabled || !outputView_) return;

    VkDescriptorSet set = inputSets_[frameSlot_];
    frameSlot_ = 1 - frameSlot_;
    VkDescriptorImageInfo imgInfo{ sampler_, inputColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = set; write.dstBinding = 0; write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    VkClearValue clear{}; clear.color = { {0,0,0,1} };
    VkRenderPassBeginInfo rpBegin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpBegin.renderPass = renderPass_; rpBegin.framebuffer = framebuffer_;
    rpBegin.renderArea.extent = { width_, height_ };
    rpBegin.clearValueCount = 1; rpBegin.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkViewport vp{ 0, 0, (float)width_, (float)height_, 0.0f, 1.0f };
    VkRect2D sc{ {0,0}, {width_, height_} };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &set, 0, nullptr);

    PushConsts pc{ exposure, contrast, saturation, 0.0f, colorFilter[0], colorFilter[1], colorFilter[2] };
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
}

void ColorCorrect::Shutdown(VkDevice device) {
    if (framebuffer_) vkDestroyFramebuffer(device, framebuffer_, nullptr);
    if (outputView_) vkDestroyImageView(device, outputView_, nullptr);
    if (outputImage_) vkDestroyImage(device, outputImage_, nullptr);
    if (outputMemory_) vkFreeMemory(device, outputMemory_, nullptr);
    if (pipeline_) vkDestroyPipeline(device, pipeline_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    if (setLayout_) vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
    if (descPool_) vkDestroyDescriptorPool(device, descPool_, nullptr);
    if (sampler_) vkDestroySampler(device, sampler_, nullptr);
    if (renderPass_) vkDestroyRenderPass(device, renderPass_, nullptr);
    if (vertShader_) vkDestroyShaderModule(device, vertShader_, nullptr);
    if (fragShader_) vkDestroyShaderModule(device, fragShader_, nullptr);
}
