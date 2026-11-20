#include "pipeline.h"

#include <android/log.h>
#include <vector>

#define TAG "VulkanPipeline"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// Embedded SPIR-V shaders (minimal: full-screen triangle, white fragment)
static const std::vector<uint32_t> kVertSpirv = {
    0x07230203u, 0x00010000u, 0x0000000Eu, 0x0000001Du, 0x00000000u, 0x00020010u, 0x00000000u, 0x00030013u, 0x00000000u, 0x00000001u, 0x00040003u, 0x00000006u,
    0x00000008u, 0x00000000u, 0x00030007u, 0x00000006u, 0x00000007u, 0x0000000Cu, 0x6E69616Du, 0x00000000u, 0x00030007u, 0x0000000Au, 0x0000000Bu, 0x0000000Cu,
    0x565F6C67u, 0x65747265u, 0x646E4978u, 0x00007865u, 0x00030007u, 0x0000000Du, 0x0000000Eu, 0x0000000Cu, 0x505F6C67u, 0x7469736Fu, 0x006E6F69u, 0x00020014u,
    0x00000001u, 0x00030015u, 0x00000002u, 0x00000020u, 0x00040017u, 0x00000003u, 0x00000002u, 0x00000004u, 0x00020016u, 0x00000005u, 0x00040015u, 0x00000004u,
    0x00000020u, 0x00000001u, 0x00040019u, 0x00000009u, 0x00000004u, 0x00000000u, 0x00040019u, 0x0000000Cu, 0x00000003u, 0x00000001u, 0x0004002Bu, 0x0000000Fu,
    0x00000004u, 0x00000000u, 0x0004002Bu, 0x00000010u, 0x00000004u, 0x00000001u, 0x0004002Bu, 0x00000011u, 0x00000002u, 0xBF800000u, 0x0004002Bu, 0x00000012u,
    0x00000002u, 0x00000000u, 0x0004002Bu, 0x00000013u, 0x00000002u, 0x3F800000u, 0x0007002Du, 0x00000014u, 0x00000003u, 0x00000011u, 0x00000011u, 0x00000012u,
    0x00000013u, 0x0007002Du, 0x00000015u, 0x00000003u, 0x00000013u, 0x00000011u, 0x00000012u, 0x00000013u, 0x0007002Du, 0x00000016u, 0x00000003u, 0x00000012u,
    0x00000013u, 0x00000012u, 0x00000013u, 0x0004003Bu, 0x00000009u, 0x0000000Au, 0x00000004u, 0x0004003Bu, 0x0000000Cu, 0x0000000Du, 0x00000003u, 0x00040047u,
    0x0000000Du, 0x00000000u, 0x00000000u, 0x00040047u, 0x0000000Au, 0x00000000u, 0x00000004u, 0x00040036u, 0x00000006u, 0x00000005u, 0x00000001u, 0x000200F9u,
    0x00000000u, 0x00040033u, 0x0000000Fu, 0x00000004u, 0x0000000Au, 0x00050034u, 0x00000017u, 0x00000004u, 0x0000000Fu, 0x0000000Fu, 0x00050034u, 0x00000018u,
    0x00000004u, 0x0000000Fu, 0x00000010u, 0x0006002Eu, 0x00000019u, 0x00000002u, 0x00000018u, 0x00000013u, 0x00000012u, 0x0006002Eu, 0x0000001Au, 0x00000002u,
    0x00000017u, 0x00000011u, 0x00000019u, 0x0006002Eu, 0x0000001Bu, 0x00000002u, 0x00000018u, 0x00000011u, 0x00000013u, 0x0006002Eu, 0x0000001Bu, 0x00000002u,
    0x00000017u, 0x00000011u, 0x0000001Bu, 0x0007002Du, 0x0000001Cu, 0x00000003u, 0x0000001Au, 0x0000001Bu, 0x00000012u, 0x00000013u, 0x00030040u, 0x0000000Du,
    0x0000001Cu, 0x00010039u, 0x00010038u,
};

static const std::vector<uint32_t> kFragSpirv = {
    0x07230203u, 0x00010000u, 0x0000000Eu, 0x0000000Du, 0x00000000u, 0x00020010u, 0x00000000u, 0x00030013u, 0x00000000u, 0x00000001u, 0x00040003u, 0x00000005u,
    0x00000007u, 0x00000000u, 0x00030007u, 0x00000005u, 0x00000006u, 0x0000000Cu, 0x6E69616Du, 0x00000000u, 0x00030007u, 0x00000009u, 0x0000000Au, 0x0000000Cu,
    0x67617266u, 0x6F6C6F43u, 0x00000072u, 0x00020014u, 0x00000001u, 0x00030015u, 0x00000002u, 0x00000020u, 0x00040017u, 0x00000003u, 0x00000002u, 0x00000004u,
    0x00020016u, 0x00000004u, 0x00040019u, 0x00000008u, 0x00000003u, 0x00000001u, 0x0004002Bu, 0x0000000Bu, 0x00000002u, 0x3F800000u, 0x0007002Du, 0x0000000Cu,
    0x00000003u, 0x0000000Bu, 0x0000000Bu, 0x0000000Bu, 0x0000000Bu, 0x0004003Bu, 0x00000008u, 0x00000009u, 0x00000003u, 0x00040047u, 0x00000009u, 0x00000001u,
    0x00000000u, 0x00040036u, 0x00000005u, 0x00000004u, 0x00000001u, 0x000200F9u, 0x00000000u, 0x00030040u, 0x00000009u, 0x0000000Cu, 0x00010039u, 0x00010038u,
};

VulkanPipeline::~VulkanPipeline() {
    // shutdown is called explicitly
}

bool VulkanPipeline::init(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
                          VkDescriptorSetLayout descriptorSetLayout) {
    device_ = device;

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        LOGE("Failed to create pipeline layout");
        return false;
    }

    // Create shader modules from embedded SPIR-V
    VkShaderModule vertModule = createShaderModule(kVertSpirv);
    VkShaderModule fragModule = createShaderModule(kFragSpirv);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        LOGE("Failed to create shader modules");
        if (vertModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, vertModule, nullptr);
        if (fragModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, fragModule, nullptr);
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
        return false;
    }

    // Create geometry pipeline (subpass 0)
    if (!createGraphicsPipeline(renderPass, 0, vertModule, fragModule, geometryPipeline_)) {
        LOGE("Failed to create geometry pipeline");
        vkDestroyShaderModule(device_, vertModule, nullptr);
        vkDestroyShaderModule(device_, fragModule, nullptr);
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
        return false;
    }

    // Create lighting pipeline (subpass 1)
    if (!createGraphicsPipeline(renderPass, 1, vertModule, fragModule, lightingPipeline_)) {
        LOGE("Failed to create lighting pipeline");
        vkDestroyPipeline(device_, geometryPipeline_, nullptr);
        geometryPipeline_ = VK_NULL_HANDLE;
        vkDestroyShaderModule(device_, vertModule, nullptr);
        vkDestroyShaderModule(device_, fragModule, nullptr);
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
        return false;
    }

    // Shader modules no longer needed after pipeline creation
    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    LOGI("Vulkan pipelines created (geometry + lighting)");
    return true;
}

void VulkanPipeline::shutdown() {
    if (geometryPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, geometryPipeline_, nullptr);
    }
    if (lightingPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, lightingPipeline_, nullptr);
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    }
}

bool VulkanPipeline::createGraphicsPipeline(VkRenderPass renderPass, uint32_t subpass,
                                             VkShaderModule vertModule, VkShaderModule fragModule,
                                             VkPipeline& outPipeline) {
    // Pipeline shader stages
    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth/stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = subpass;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create graphics pipeline: %d", result);
        return false;
    }

    return true;
}

VkShaderModule VulkanPipeline::createShaderModule(const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        LOGE("Failed to create shader module");
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}
