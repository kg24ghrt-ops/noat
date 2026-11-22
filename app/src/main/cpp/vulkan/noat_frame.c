/*
 * noat_frame.c - Frame management (command buffers, sync objects)
 */

#include "noat_vulkan.h"
#include <android/log.h>
#include <stdlib.h>

#define TAG "NoatFrame"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

struct NoatFrame {
    VkDevice device;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore image_available_semaphore;
    VkSemaphore render_finished_semaphore;
    VkFence in_flight_fence;
};

NoatFrame *noat_frame_create(VkDevice device, uint32_t queue_family) {
    NoatFrame *frame = calloc(1, sizeof(NoatFrame));
    if (!frame) return NULL;

    frame->device = device;

    if (vkCreateCommandPool(device, &(VkCommandPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = queue_family,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    }, NULL, &frame->command_pool) != VK_SUCCESS) {
        LOGE("Failed to create command pool");
        free(frame);
        return NULL;
    }

    if (vkAllocateCommandBuffers(frame->device, &(VkCommandBufferAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = frame->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    }, &frame->command_buffer) != VK_SUCCESS) {
        LOGE("Failed to allocate command buffer");
        vkDestroyCommandPool(device, frame->command_pool, NULL);
        free(frame);
        return NULL;
    }

    if (vkCreateSemaphore(device, &(VkSemaphoreCreateInfo){.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}, NULL, &frame->image_available_semaphore) != VK_SUCCESS ||
        vkCreateSemaphore(device, &(VkSemaphoreCreateInfo){.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}, NULL, &frame->render_finished_semaphore) != VK_SUCCESS ||
        vkCreateFence(device, &(VkFenceCreateInfo){.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT}, NULL, &frame->in_flight_fence) != VK_SUCCESS) {
        LOGE("Failed to create sync objects");
        noat_frame_destroy(frame);
        return NULL;
    }

    return frame;
}

void noat_frame_destroy(NoatFrame *frame) {
    if (!frame) return;
    if (frame->in_flight_fence) vkDestroyFence(frame->device, frame->in_flight_fence, NULL);
    if (frame->render_finished_semaphore) vkDestroySemaphore(frame->device, frame->render_finished_semaphore, NULL);
    if (frame->image_available_semaphore) vkDestroySemaphore(frame->device, frame->image_available_semaphore, NULL);
    if (frame->command_pool) vkDestroyCommandPool(frame->device, frame->command_pool, NULL);
    free(frame);
}

VkCommandBuffer noat_frame_get_command_buffer(const NoatFrame *frame) {
    return frame ? frame->command_buffer : VK_NULL_HANDLE;
}

VkSemaphore noat_frame_get_image_available_semaphore(const NoatFrame *frame) {
    return frame ? frame->image_available_semaphore : VK_NULL_HANDLE;
}

VkSemaphore noat_frame_get_render_finished_semaphore(const NoatFrame *frame) {
    return frame ? frame->render_finished_semaphore : VK_NULL_HANDLE;
}

VkFence noat_frame_get_in_flight_fence(const NoatFrame *frame) {
    return frame ? frame->in_flight_fence : VK_NULL_HANDLE;
}

void noat_frame_begin_command_buffer(NoatFrame *frame) {
    vkResetCommandBuffer(frame->command_buffer, 0);
    vkBeginCommandBuffer(frame->command_buffer, &(VkCommandBufferBeginInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    });
}

void noat_frame_end_command_buffer(NoatFrame *frame) {
    vkEndCommandBuffer(frame->command_buffer);
}

VkFence noat_frame_get_in_flight_fence(const NoatFrame *frame) {
    return frame ? frame->in_flight_fence : VK_NULL_HANDLE;
}

VkSemaphore noat_frame_get_image_available_semaphore(const NoatFrame *frame) {
    return frame ? frame->image_available_semaphore : VK_NULL_HANDLE;
}

VkSemaphore noat_frame_get_render_finished_semaphore(const NoatFrame *frame) {
    return frame ? frame->render_finished_semaphore : VK_NULL_HANDLE;
}