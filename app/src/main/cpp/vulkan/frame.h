#pragma once

#include <vulkan/vulkan.h>
#include <vector>

// Per-frame Vulkan resources: command buffer, semaphores, fence
class VulkanFrame {
public:
    VulkanFrame() = default;
    ~VulkanFrame();

    VulkanFrame(const VulkanFrame&) = delete;
    VulkanFrame& operator=(const VulkanFrame&) = delete;

    bool init(VkDevice device, uint32_t queueFamilyIndex);
    void shutdown();

    VkCommandBuffer commandBuffer() const { return commandBuffer_; }
    VkSemaphore imageAvailableSemaphore() const { return imageAvailableSemaphore_; }
    VkSemaphore renderFinishedSemaphore() const { return renderFinishedSemaphore_; }
    VkFence inFlightFence() const { return inFlightFence_; }

    bool beginCommandBuffer();
    bool endCommandBuffer();

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkSemaphore imageAvailableSemaphore_ = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore_ = VK_NULL_HANDLE;
    VkFence inFlightFence_ = VK_NULL_HANDLE;
};
