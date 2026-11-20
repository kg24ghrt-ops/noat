#include "renderpass.h"

#include <android/log.h>
#include <array>

#define TAG "VulkanRenderPass"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

VulkanRenderPass::~VulkanRenderPass() {
    // shutdown is called explicitly
}

bool VulkanRenderPass::init(VkDevice device, VkFormat colorFormat, VkFormat depthFormat) {
    device_ = device;

    // Attachment descriptions
    // 0: Color (G-buffer albedo)
    // 1: Normal (G-buffer normals)
    // 2: Depth
    // 3: Swapchain image (final output)
    std::array<VkAttachmentDescription, 4> attachments = {};

    // Color attachment (subpass 0 output, subpass 1 input)
    attachments[AttachmentColor] = {
        .flags = 0,
        .format = colorFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,  // doesn't need to persist
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,  // read as input in subpass 1
    };

    // Normal attachment (subpass 0 output, subpass 1 input)
    attachments[AttachmentNormal] = {
        .flags = 0,
        .format = colorFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    // Depth attachment (subpass 0 output, subpass 1 input)
    attachments[AttachmentDepth] = {
        .flags = 0,
        .format = depthFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    // Swapchain color attachment (subpass 1 output)
    attachments[AttachmentSwapchain] = {
        .flags = 0,
        .format = colorFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    // Subpass 0: Geometry - writes color, normal, depth
    std::array<VkAttachmentReference, 3> geometryColorRefs = {
        VkAttachmentReference{.attachment = AttachmentColor, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        VkAttachmentReference{.attachment = AttachmentNormal, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        VkAttachmentReference{.attachment = AttachmentDepth, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
    };

    VkSubpassDescription geometrySubpass = {
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = static_cast<uint32_t>(geometryColorRefs.size()),
        .pColorAttachments = geometryColorRefs.data(),
        .pDepthStencilAttachment = &geometryColorRefs[2],  // depth is index 2
    };

    // Subpass 1: Lighting - reads color/normal/depth as inputs, writes to swapchain
    std::array<VkAttachmentReference, 3> lightingInputRefs = {
        VkAttachmentReference{.attachment = AttachmentColor, .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        VkAttachmentReference{.attachment = AttachmentNormal, .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        VkAttachmentReference{.attachment = AttachmentDepth, .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    };

    VkAttachmentReference lightingColorRef = {
        .attachment = AttachmentSwapchain,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription lightingSubpass = {
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = static_cast<uint32_t>(lightingInputRefs.size()),
        .pInputAttachments = lightingInputRefs.data(),
        .colorAttachmentCount = 1,
        .pColorAttachments = &lightingColorRef,
    };

    // Subpass dependency: geometry -> lighting (in-tile via input attachments)
    std::array<VkSubpassDependency, 2> dependencies = {};

    // External -> geometry
    dependencies[0] = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = SubpassGeometry,
        .srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
    };

    // Geometry -> lighting (via input attachments)
    dependencies[1] = {
        .srcSubpass = SubpassGeometry,
        .dstSubpass = SubpassLighting,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
    };

    // Lighting -> External (present)
    // The swapchain attachment's final layout transition happens at the end

    VkRenderPassCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast<uint32_t>(attachments.size()),
        .pAttachments = attachments.data(),
        .subpassCount = 2,
        .pSubpasses = &geometrySubpass,
        .dependencyCount = static_cast<uint32_t>(dependencies.size()),
        .pDependencies = dependencies.data(),
    };

    // We need to pass both subpasses
    VkSubpassDescription subpasses[2] = {geometrySubpass, lightingSubpass};
    createInfo.pSubpasses = subpasses;
    createInfo.subpassCount = 2;

    VkResult result = vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create render pass: %d", result);
        return false;
    }

    LOGI("Render pass created with 2 subpasses (geometry + lighting)");
    return true;
}

void VulkanRenderPass::shutdown() {
    if (renderPass_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
}
