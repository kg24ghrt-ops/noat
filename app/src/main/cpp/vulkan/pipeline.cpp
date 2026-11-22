#include "pipeline.h"

#include <android/log.h>
#include <vector>

#define TAG "VulkanPipeline"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// Embedded SPIR-V shaders compiled from GLSL with glslangValidator
static const std::vector<uint32_t> kVertSpirv = {
    0x07230203u, 0x00010000u, 0x0008000Bu, 0x00000063u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000Bu,
    0x00000001u, 0x4C534C47u, 0x6474732Eu, 0x3035342Eu, 0x00000000u, 0x0003000Eu, 0x00000000u, 0x00000001u,
    0x0007000Fu, 0x00000000u, 0x00000004u, 0x6E69616Du, 0x00000000u, 0x00000008u, 0x0000005Au, 0x00030003u,
    0x00000002u, 0x000001C2u, 0x00090004u, 0x415F4C47u, 0x735F4252u, 0x72617065u, 0x5F657461u, 0x64616873u,
    0x6F5F7265u, 0x63656A62u, 0x00007374u, 0x00040005u, 0x00000004u, 0x6E69616Du, 0x00000000u, 0x00060005u,
    0x00000008u, 0x565F6C67u, 0x65747265u, 0x646E4978u, 0x00007865u, 0x00030005u, 0x00000012u, 0x00000070u,
    0x00060005u, 0x00000013u, 0x68737550u, 0x736E6F43u, 0x746E6174u, 0x00000073u, 0x00040006u, 0x00000013u,
    0x00000000u, 0x00007871u, 0x00040006u, 0x00000013u, 0x00000001u, 0x00007971u, 0x00030005u, 0x00000015u,
    0x00006370u, 0x00060005u, 0x00000058u, 0x505F6C67u, 0x65567265u, 0x78657472u, 0x00000000u, 0x00060006u,
    0x00000058u, 0x00000000u, 0x505F6C67u, 0x7469736Fu, 0x006E6F69u, 0x00070006u, 0x00000058u, 0x00000001u,
    0x505F6C67u, 0x746E696Fu, 0x657A6953u, 0x00000000u, 0x00070006u, 0x00000058u, 0x00000002u, 0x435F6C67u,
    0x4470696Cu, 0x61747369u, 0x0065636Eu, 0x00070006u, 0x00000058u, 0x00000003u, 0x435F6C67u, 0x446C6C75u,
    0x61747369u, 0x0065636Eu, 0x00030005u, 0x0000005Au, 0x00000000u, 0x00040047u, 0x00000008u, 0x0000000Bu,
    0x0000002Au, 0x00030047u, 0x00000013u, 0x00000002u, 0x00050048u, 0x00000013u, 0x00000000u, 0x00000023u,
    0x00000000u, 0x00050048u, 0x00000013u, 0x00000001u, 0x00000023u, 0x00000004u, 0x00030047u, 0x00000058u,
    0x00000002u, 0x00050048u, 0x00000058u, 0x00000000u, 0x0000000Bu, 0x00000000u, 0x00050048u, 0x00000058u,
    0x00000001u, 0x0000000Bu, 0x00000001u, 0x00050048u, 0x00000058u, 0x00000002u, 0x0000000Bu, 0x00000003u,
    0x00050048u, 0x00000058u, 0x00000003u, 0x0000000Bu, 0x00000004u, 0x00020013u, 0x00000002u, 0x00030021u,
    0x00000003u, 0x00000002u, 0x00040015u, 0x00000006u, 0x00000020u, 0x00000001u, 0x00040020u, 0x00000007u,
    0x00000001u, 0x00000006u, 0x0004003Bu, 0x00000007u, 0x00000008u, 0x00000001u, 0x0004002Bu, 0x00000006u,
    0x0000000Au, 0x00000000u, 0x00020014u, 0x0000000Bu, 0x00030016u, 0x0000000Fu, 0x00000020u, 0x00040017u,
    0x00000010u, 0x0000000Fu, 0x00000002u, 0x00040020u, 0x00000011u, 0x00000007u, 0x00000010u, 0x0004001Eu,
    0x00000013u, 0x0000000Fu, 0x0000000Fu, 0x00040020u, 0x00000014u, 0x00000009u, 0x00000013u, 0x0004003Bu,
    0x00000014u, 0x00000015u, 0x00000009u, 0x00040020u, 0x00000016u, 0x00000009u, 0x0000000Fu, 0x0004002Bu,
    0x00000006u, 0x0000001Au, 0x00000001u, 0x0004002Bu, 0x00000006u, 0x0000002Au, 0x00000002u, 0x0004002Bu,
    0x00000006u, 0x00000036u, 0x00000003u, 0x0004002Bu, 0x00000006u, 0x00000042u, 0x00000004u, 0x00040017u,
    0x00000054u, 0x0000000Fu, 0x00000004u, 0x00040015u, 0x00000055u, 0x00000020u, 0x00000000u, 0x0004002Bu,
    0x00000055u, 0x00000056u, 0x00000001u, 0x0004001Cu, 0x00000057u, 0x0000000Fu, 0x00000056u, 0x0006001Eu,
    0x00000058u, 0x00000054u, 0x0000000Fu, 0x00000057u, 0x00000057u, 0x00040020u, 0x00000059u, 0x00000003u,
    0x00000058u, 0x0004003Bu, 0x00000059u, 0x0000005Au, 0x00000003u, 0x0004002Bu, 0x0000000Fu, 0x0000005Cu,
    0x00000000u, 0x0004002Bu, 0x0000000Fu, 0x0000005Du, 0x3F800000u, 0x00040020u, 0x00000061u, 0x00000003u,
    0x00000054u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200F8u, 0x00000005u,
    0x0004003Bu, 0x00000011u, 0x00000012u, 0x00000007u, 0x0004003Du, 0x00000006u, 0x00000009u, 0x00000008u,
    0x000500AAu, 0x0000000Bu, 0x0000000Cu, 0x00000009u, 0x0000000Au, 0x000300F7u, 0x0000000Eu, 0x00000000u,
    0x000400FAu, 0x0000000Cu, 0x0000000Du, 0x0000001Eu, 0x000200F8u, 0x0000000Du, 0x00050041u, 0x00000016u,
    0x00000017u, 0x00000015u, 0x0000000Au, 0x0004003Du, 0x0000000Fu, 0x00000018u, 0x00000017u, 0x0004007Fu,
    0x0000000Fu, 0x00000019u, 0x00000018u, 0x00050041u, 0x00000016u, 0x0000001Bu, 0x00000015u, 0x0000001Au,
    0x0004003Du, 0x0000000Fu, 0x0000001Cu, 0x0000001Bu, 0x00050050u, 0x00000010u, 0x0000001Du, 0x00000019u,
    0x0000001Cu, 0x0003003Eu, 0x00000012u, 0x0000001Du, 0x000200F9u, 0x0000000Eu, 0x000200F8u, 0x0000001Eu,
    0x0004003Du, 0x00000006u, 0x0000001Fu, 0x00000008u, 0x000500AAu, 0x0000000Bu, 0x00000020u, 0x0000001Fu,
    0x0000001Au, 0x000300F7u, 0x00000022u, 0x00000000u, 0x000400FAu, 0x00000020u, 0x00000021u, 0x00000028u,
    0x000200F8u, 0x00000021u, 0x00050041u, 0x00000016u, 0x00000023u, 0x00000015u, 0x0000000Au, 0x0004003Du,
    0x0000000Fu, 0x00000024u, 0x00000023u, 0x00050041u, 0x00000016u, 0x00000025u, 0x00000015u, 0x0000001Au,
    0x0004003Du, 0x0000000Fu, 0x00000026u, 0x00000025u, 0x00050050u, 0x00000010u, 0x00000027u, 0x00000024u,
    0x00000026u, 0x0003003Eu, 0x00000012u, 0x00000027u, 0x000200F9u, 0x00000022u, 0x000200F8u, 0x00000028u,
    0x0004003Du, 0x00000006u, 0x00000029u, 0x00000008u, 0x000500AAu, 0x0000000Bu, 0x0000002Bu, 0x00000029u,
    0x0000002Au, 0x000300F7u, 0x0000002Du, 0x00000000u, 0x000400FAu, 0x0000002Bu, 0x0000002Cu, 0x00000034u,
    0x000200F8u, 0x0000002Cu, 0x00050041u, 0x00000016u, 0x0000002Eu, 0x00000015u, 0x0000000Au, 0x0004003Du,
    0x0000000Fu, 0x0000002Fu, 0x0000002Eu, 0x00050041u, 0x00000016u, 0x00000030u, 0x00000015u, 0x0000001Au,
    0x0004003Du, 0x0000000Fu, 0x00000031u, 0x00000030u, 0x0004007Fu, 0x0000000Fu, 0x00000032u, 0x00000031u,
    0x00050050u, 0x00000010u, 0x00000033u, 0x0000002Fu, 0x00000032u, 0x0003003Eu, 0x00000012u, 0x00000033u,
    0x000200F9u, 0x0000002Du, 0x000200F8u, 0x00000034u, 0x0004003Du, 0x00000006u, 0x00000035u, 0x00000008u,
    0x000500AAu, 0x0000000Bu, 0x00000037u, 0x00000035u, 0x00000036u, 0x000300F7u, 0x00000039u, 0x00000000u,
    0x000400FAu, 0x00000037u, 0x00000038u, 0x00000040u, 0x000200F8u, 0x00000038u, 0x00050041u, 0x00000016u,
    0x0000003Au, 0x00000015u, 0x0000000Au, 0x0004003Du, 0x0000000Fu, 0x0000003Bu, 0x0000003Au, 0x0004007Fu,
    0x0000000Fu, 0x0000003Cu, 0x0000003Bu, 0x00050041u, 0x00000016u, 0x0000003Du, 0x00000015u, 0x0000001Au,
    0x0004003Du, 0x0000000Fu, 0x0000003Eu, 0x0000003Du, 0x00050050u, 0x00000010u, 0x0000003Fu, 0x0000003Cu,
    0x0000003Eu, 0x0003003Eu, 0x00000012u, 0x0000003Fu, 0x000200F9u, 0x00000039u, 0x000200F8u, 0x00000040u,
    0x0004003Du, 0x00000006u, 0x00000041u, 0x00000008u, 0x000500AAu, 0x0000000Bu, 0x00000043u, 0x00000041u,
    0x00000042u, 0x000300F7u, 0x00000045u, 0x00000000u, 0x000400FAu, 0x00000043u, 0x00000044u, 0x0000004Cu,
    0x000200F8u, 0x00000044u, 0x00050041u, 0x00000016u, 0x00000046u, 0x00000015u, 0x0000000Au, 0x0004003Du,
    0x0000000Fu, 0x00000047u, 0x00000046u, 0x00050041u, 0x00000016u, 0x00000048u, 0x00000015u, 0x0000001Au,
    0x0004003Du, 0x0000000Fu, 0x00000049u, 0x00000048u, 0x0004007Fu, 0x0000000Fu, 0x0000004Au, 0x00000049u,
    0x00050050u, 0x00000010u, 0x0000004Bu, 0x00000047u, 0x0000004Au, 0x0003003Eu, 0x00000012u, 0x0000004Bu,
    0x000200F9u, 0x00000045u, 0x000200F8u, 0x0000004Cu, 0x00050041u, 0x00000016u, 0x0000004Du, 0x00000015u,
    0x0000000Au, 0x0004003Du, 0x0000000Fu, 0x0000004Eu, 0x0000004Du, 0x0004007Fu, 0x0000000Fu, 0x0000004Fu,
    0x0000004Eu, 0x00050041u, 0x00000016u, 0x00000050u, 0x00000015u, 0x0000001Au, 0x0004003Du, 0x0000000Fu,
    0x00000051u, 0x00000050u, 0x0004007Fu, 0x0000000Fu, 0x00000052u, 0x00000051u, 0x00050050u, 0x00000010u,
    0x00000053u, 0x0000004Fu, 0x00000052u, 0x0003003Eu, 0x00000012u, 0x00000053u, 0x000200F9u, 0x00000045u,
    0x000200F8u, 0x00000045u, 0x000200F9u, 0x00000039u, 0x000200F8u, 0x00000039u, 0x000200F9u, 0x0000002Du,
    0x000200F8u, 0x0000002Du, 0x000200F9u, 0x00000022u, 0x000200F8u, 0x00000022u, 0x000200F9u, 0x0000000Eu,
    0x000200F8u, 0x0000000Eu, 0x0004003Du, 0x00000010u, 0x0000005Bu, 0x00000012u, 0x00050051u, 0x0000000Fu,
    0x0000005Eu, 0x0000005Bu, 0x00000000u, 0x00050051u, 0x0000000Fu, 0x0000005Fu, 0x0000005Bu, 0x00000001u,
    0x00070050u, 0x00000054u, 0x00000060u, 0x0000005Eu, 0x0000005Fu, 0x0000005Cu, 0x0000005Du, 0x00050041u,
    0x00000061u, 0x00000062u, 0x0000005Au, 0x0000000Au, 0x0003003Eu, 0x00000062u, 0x00000060u, 0x000100FDu,
    0x00010038u,
};

static const std::vector<uint32_t> kFragSpirv = {
    0x07230203u, 0x00010000u, 0x0008000Bu, 0x0000000Eu, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000Bu,
    0x00000001u, 0x4C534C47u, 0x6474732Eu, 0x3035342Eu, 0x00000000u, 0x0003000Eu, 0x00000000u, 0x00000001u,
    0x0006000Fu, 0x00000004u, 0x00000004u, 0x6E69616Du, 0x00000000u, 0x00000009u, 0x00030010u, 0x00000004u,
    0x00000007u, 0x00030003u, 0x00000002u, 0x000001C2u, 0x00090004u, 0x415F4C47u, 0x735F4252u, 0x72617065u,
    0x5F657461u, 0x64616873u, 0x6F5F7265u, 0x63656A62u, 0x00007374u, 0x00040005u, 0x00000004u, 0x6E69616Du,
    0x00000000u, 0x00040005u, 0x00000009u, 0x6C6F436Fu, 0x0000726Fu, 0x00040047u, 0x00000009u, 0x0000001Eu,
    0x00000000u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u,
    0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u, 0x00000004u, 0x00040020u, 0x00000008u, 0x00000003u,
    0x00000007u, 0x0004003Bu, 0x00000008u, 0x00000009u, 0x00000003u, 0x0004002Bu, 0x00000006u, 0x0000000Au,
    0x3F70A3D7u, 0x0004002Bu, 0x00000006u, 0x0000000Bu, 0x3F666666u, 0x0004002Bu, 0x00000006u, 0x0000000Cu,
    0x3F800000u, 0x0007002Cu, 0x00000007u, 0x0000000Du, 0x0000000Au, 0x0000000Au, 0x0000000Bu, 0x0000000Cu,
    0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200F8u, 0x00000005u, 0x0003003Eu,
    0x00000009u, 0x0000000Du, 0x000100FDu, 0x00010038u,
};

VulkanPipeline::~VulkanPipeline() {
    shutdown();
}

bool VulkanPipeline::init(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
                          VkDescriptorSetLayout descriptorSetLayout) {
    device_ = device;

    // Create pipeline layout from the descriptor set layout
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstantBlock);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

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

    // Vertex input: none (uses gl_VertexIndex)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor (dynamic)
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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth/stencil: disabled
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

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
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create graphics pipeline: %d", result);
        vkDestroyShaderModule(device_, vertModule, nullptr);
        vkDestroyShaderModule(device_, fragModule, nullptr);
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
        return false;
    }

    // Shader modules no longer needed after pipeline creation
    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    LOGI("Vulkan pipeline created");
    return true;
}

void VulkanPipeline::shutdown() {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
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
