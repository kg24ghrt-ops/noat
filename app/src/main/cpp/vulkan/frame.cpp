#include "frame.h"

#include <android/log.h>

#define TAG "VulkanFrame"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

VulkanFrame::~VulkanFrame() {
    // shutdown is called explicitly
}

bool VulkanFrame::init(VkDevice device, uint32_t queueFamilyIndex) {
    device_ = device;

    // Command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkCommandPool commandPool;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        LOGE("Failed to create command pool");
        return false;
    }

    // Command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer_) != VK_SUCCESS) {
        LOGE("Failed to allocate command buffer");
        vkDestroyCommandPool(device, commandPool, nullptr);
        return false;
    }

    // Semaphores
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore_) != VK_SUCCESS ||
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore_) != VK_SUCCESS) {
        LOGE("Failed to create semaphores");
        return false;
    }

    // Fence
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // start signaled so first wait doesn't block

    if (vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence_) != VK_SUCCESS) {
        LOGE("Failed to create fence");
        return false;
    }

    LOGI("Vulkan frame resources created");
    return true;
}

void VulkanFrame::shutdown() {
    if (device_ == VK_NULL_HANDLE) return;

    if (inFlightFence_ != VK_NULL_HANDLE) {
        vkDestroyFence(device_, inFlightFence_, nullptr);
    }
    if (renderFinishedSemaphore_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, renderFinishedSemaphore_, nullptr);
    }
    if (imageAvailableSemaphore_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, imageAvailableSemaphore_, nullptr);
    }
}

bool VulkanFrame::beginCommandBuffer() {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    return vkBeginCommandBuffer(commandBuffer_, &beginInfo) == VK_SUCCESS;
}

bool VulkanFrame::endCommandBuffer() {
    return vkEndCommandBuffer(commandBuffer_) == VK_SUCCESS;
}
