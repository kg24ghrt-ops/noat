#pragma once

#include <vulkan/vulkan.h>

// Render pass with two subpasses optimized for Mali tile-based rendering:
//   Subpass 0: Geometry - renders paper surface to color + normal + depth attachments
//   Subpass 1: Lighting - reads color/normal/depth as input attachments (on-chip tile memory)
//             outputs final lit color
class VulkanRenderPass {
public:
    VulkanRenderPass() = default;
    ~VulkanRenderPass();

    VulkanRenderPass(const VulkanRenderPass&) = delete;
    VulkanRenderPass& operator=(const VulkanRenderPass&) = delete;

    bool init(VkDevice device, VkFormat colorFormat, VkFormat depthFormat);
    void shutdown();

    VkRenderPass renderPass() const { return renderPass_; }

    // Attachment indices
    static constexpr uint32_t AttachmentColor = 0;
    static constexpr uint32_t AttachmentNormal = 1;
    static constexpr uint32_t AttachmentDepth = 2;
    static constexpr uint32_t AttachmentSwapchain = 3;

    static constexpr uint32_t SubpassGeometry = 0;
    static constexpr uint32_t SubpassLighting = 1;

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
};
