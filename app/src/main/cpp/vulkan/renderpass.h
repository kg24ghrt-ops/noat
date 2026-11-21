#pragma once

#include <vulkan/vulkan.h>

// Single-subpass render pass for rendering the paper surface directly to swapchain.
class VulkanRenderPass {
public:
    VulkanRenderPass() = default;
    ~VulkanRenderPass();

    VulkanRenderPass(const VulkanRenderPass&) = delete;
    VulkanRenderPass& operator=(const VulkanRenderPass&) = delete;

    bool init(VkDevice device, VkFormat colorFormat);
    void shutdown();

    VkRenderPass renderPass() const { return renderPass_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
};
