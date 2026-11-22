/*
 * noat_renderer.c - Main paper renderer
 */

#include "noat_vulkan.h"
#include "noat_renderer.h"
#include "noat_context.h"
#include "noat_renderpass.h"
#include "noat_pipeline.h"
#include "noat_resources.h"
#include "noat_frame.h"
#include "noat_paper_geometry.h"

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <stdlib.h>
#include <string.h>

#define TAG "PaperRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

struct NoatRenderer {
    NoatContext *context;
    NoatRenderPass *render_pass;
    NoatPipeline *pipeline;
    NoatResources *resources;
    NoatFrame **frames;
    VkCommandPool command_pool;
    uint32_t current_frame;
    int initialized;
    NoatLightParams light;
    NoatMaterialParams material;
    const struct NoatPaperSize *current_size;
};

NoatRenderer *noat_renderer_create(void) {
    return calloc(1, sizeof(NoatRenderer));
}

void noat_renderer_destroy(NoatRenderer *r) {
    if (!r) return;
    noat_renderer_shutdown(r);
    free(r);
}

int noat_renderer_init(NoatRenderer *r, ANativeWindow *window) {
    LOGI("PaperRenderer::init called");

    r->context = noat_context_create(&(NoatContextConfig){
        .window = window,
        .width = 0,
        .height = 0,
        .enable_validation = 1,
    });
    if (!r->context || !r->context->initialized) {
        LOGE("Failed to initialize Vulkan context");
        return 0;
    }
    LOGI("Vulkan context initialized, format=%d, size=%dx%d, imageCount=%zu",
         r->context->swapchain_format, r->context->width, r->context->height,
         r->context->image_count);

    r->render_pass = noat_renderpass_create(r->context->device, r->context->swapchain_format);
    if (!r->render_pass) {
        LOGE("Failed to create render pass");
        return 0;
    }
    LOGI("Render pass created");

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = r->context->graphics_queue_family,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };
    if (vkCreateCommandPool(r->context->device, &pool_info, NULL, &r->command_pool) != VK_SUCCESS) {
        LOGE("Failed to create command pool");
        return 0;
    }
    LOGI("Command pool created");

    r->resources = noat_resources_create(r->context->device, r->context->physical_device,
                                          r->command_pool, r->context->graphics_queue,
                                          r->context->image_count);
    if (!r->resources) {
        LOGE("Failed to create resources");
        return 0;
    }
    LOGI("Resources created");

    r->pipeline = noat_pipeline_create(r->context->device, r->render_pass->render_pass,
                                        (VkExtent2D){r->context->width, r->context->height},
                                        r->resources->descriptor_set_layout);
    if (!r->pipeline) {
        LOGE("Failed to create pipeline");
        return 0;
    }
    LOGI("Pipeline created");

    uint32_t image_count = r->context->image_count;
    r->frames = calloc(image_count, sizeof(NoatFrame*));
    for (uint32_t i = 0; i < image_count; i++) {
        r->frames[i] = noat_frame_create(r->context->device, r->context->graphics_queue_family);
        if (!r->frames[i]) {
            LOGE("Failed to initialize frame %u", i);
            return 0;
        }
    }
    LOGI("Frames created (%u)", image_count);

    // Default lighting
    NoatLightParams light = {
        .direction = {0.5f, -1.0f, 0.5f},
        .color = {1.0f, 1.0f, 0.95f},
        .intensity = 1.0f,
        .ambient = 0.3f,
        .env_color = {0.8f, 0.85f, 0.9f},
        .env_intensity = 0.4f,
    };
    noat_renderer_set_light(r, &light);

    // Default material: standard notebook paper
    NoatMaterialParams material = {
        .color = {0.92f, 0.94f, 0.98f},
        .roughness = 0.4f,
        .specular_strength = 0.1f,
        .specular_roughness = 0.6f,
        .fiber_strength = 0.5f,
        .grain_scale = 1.0f,
        .thickness = 0.08f,
    };
    noat_renderer_set_material(r, &material);

    // Default to A4
    r->current_size = noat_paper_default_size();
    LOGI("Paper renderer initialized successfully, default size=%s (%.0fx%.0f mm)",
         r->current_size->name, r->current_size->width_mm, r->current_size->height_mm);
    return 1;
}

void noat_renderer_resize(NoatRenderer *r, int width, int height) {
    if (!r || !r->initialized) return;
    if (!noat_context_resize(r->context, width, height)) return;

    LOGI("Paper renderer surface %dx%d", width, height);
    r->resources->shutdown();
    // Recreate resources and frames...
    // TODO: Implement full resize logic
}

void noat_renderer_render_frame(NoatRenderer *r) {
    if (!r || !r->initialized) {
        LOGW("renderFrame called but renderer not initialized");
        return;
    }

    VkDevice device = r->context->device;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    uint32_t current_frame = r->current_frame;

    LOGI("renderFrame #%u: begin", current_frame);

    // Wait for previous frame
    VkFence wait_fence = noat_frame_get_in_flight_fence(r->frames[current_frame]);
    VkResult wait_result = vkWaitForFences(device, 1, &wait_fence, VK_TRUE, UINT64_MAX);
    if (wait_result != VK_SUCCESS) {
        LOGE("vkWaitForFences failed: %d", wait_result);
        return;
    }

    // Acquire next swapchain image
    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR(device, r->context->swapchain, UINT64_MAX,
                                            noat_frame_get_image_available_semaphore(r->frames[current_frame]),
                                            VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        VkFence reset_fence = noat_frame_get_in_flight_fence(r->frames[current_frame]);
        vkResetFences(device, 1, &reset_fence);
        return;
    }
    if (result != VK_SUCCESS) {
        LOGE("vkAcquireNextImageKHR failed: %d", result);
        return;
    }
    LOGI("renderFrame #%u: acquired image %u", current_frame, image_index);

    vkResetFences(device, 1, &noat_frame_get_in_flight_fence(r->frames[current_frame]));

    // Record command buffer
    noat_frame_begin_command_buffer(r->frames[current_frame]);
    VkCommandBuffer cmd = noat_frame_get_command_buffer(r->frames[current_frame]);

    // Create framebuffer
    VkFramebuffer framebuffer;
    VkImageView attachment = r->context->swapchain_image_views[image_index];
    VkFramebufferCreateInfo fb_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = r->render_pass->render_pass,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .width = r->context->width,
        .height = r->context->height,
        .layers = 1,
    };
    if (vkCreateFramebuffer(r->context->device, &fb_info, NULL, &framebuffer) != VK_SUCCESS) {
        LOGE("Failed to create framebuffer");
        noat_frame_end_command_buffer(r->frames[current_frame]);
        return;
    }

    VkRenderPassBeginInfo rp_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = r->render_pass->render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {r->context->width, r->context->height}},
        .clearValueCount = 1,
        .pClearValues = &(VkClearValue){{.color = {{0.0f, 0.0f, 0.0f, 1.0f}}}},
    };
    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

    // Push constants
    NoatPushConstants pc = {};
    if (r->current_size) {
        float qx, qy;
        noat_paper_compute_quad(r->current_size, r->context->width, r->context->height, &pc.qx, &pc.qy);
    }
    vkCmdPushConstants(cmd, r->pipeline->pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(NoatPushConstants), &pc);

    LOGI("renderFrame #%u: paper %s (%.0fx%.0f mm, aspect=%.4f), quad qx=%.4f qy=%.4f, surface %dx%d px",
         current_frame,
         r->current_size ? r->current_size->name : "(none)",
         r->current_size ? r->current_size->width_mm : 0.0f,
         r->current_size ? r->current_size->height_mm : 0.0f,
         r->current_size ? (r->current_size->width_mm / r->current_size->height_mm) : 0.0f,
         pc.qx, pc.qy,
         r->context->width, r->context->height);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline->pipeline);

    VkViewport viewport = {
        .x = 0, .y = 0,
        .width = (float)r->context->width(),
        .height = (float)r->context->height(),
        .minDepth = 0.0f, .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor = {{0, 0}, {r->context->width, r->context->height}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind descriptor sets
    const VkDescriptorSet *sets;
    uint32_t set_count;
    sets = noat_resources_get_descriptor_sets(r->resources, &set_count);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline->pipeline_layout,
                            0, 1, &sets[r->current_frame], 0, NULL);

    // Draw quad (6 vertices)
    vkCmdDraw(cmd, 6, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
    noat_frame_end_command_buffer(r->frames[r->current_frame]);

    // Submit
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &noat_frame_get_image_available_semaphore(r->frames[current_frame]),
        .pWaitDstStageMask = (VkPipelineStageFlags[]){VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &noat_frame_get_render_finished_semaphore(r->frames[current_frame]),
    };

    if (vkQueueSubmit(r->context->graphics_queue, 1, &submit,
                      noat_frame_get_in_flight_fence(r->frames[current_frame])) != VK_SUCCESS) {
        LOGE("Failed to submit draw command buffer");
        vkDestroyFramebuffer(device, framebuffer, NULL);
        return;
    }

    // Present
    VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &noat_frame_get_render_finished_semaphore(r->frames[current_frame]),
        .swapchainCount = 1,
        .pSwapchains = &r->context->swapchain,
        .pImageIndices = &image_index,
    };

    VkResult present_result = vkQueuePresentKHR(r->context->graphics_queue, &present_info);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
        LOGI("renderFrame #%u: present out of date/suboptimal (%d)", current_frame, present_result);
    } else if (present_result != VK_SUCCESS) {
        LOGE("vkQueuePresentKHR failed: %d", present_result);
    } else {
        LOGI("renderFrame #%u: presented successfully", current_frame);
    }

    vkDestroyFramebuffer(device, framebuffer, NULL);
    r->current_frame = (r->current_frame + 1) % r->context->image_count;
}

void noat_renderer_set_paper_size(NoatRenderer *r, const char *size_id) {
    const PaperSize *size = paper_geometry_find_size(sizeId);
    if (!size) {
        LOGE("Unknown paper size id: %s", sizeId);
        return;
    }
    r->current_size = size;
    LOGI("Paper size set to %s (%s), %.0fx%.0f mm", sizeId, size->name,
         size->width_mm, size->height_mm);
}

void noat_renderer_set_material(NoatRenderer *r, const NoatMaterialParams *material) {
    r->material = *material;
}

void noat_renderer_set_light(NoatRenderer *r, const NoatLightParams *light) {
    r->light = *light;
}

void noat_renderer_set_quality(NoatRenderer *r, int quality) {
    // TODO: implement quality settings
}

void noat_renderer_shutdown(NoatRenderer *r) {
    if (!r || !r->initialized) return;

    vkDeviceWaitIdle(r->context->device);

    for (uint32_t i = 0; i < r->context->image_count; i++) {
        if (r->frames[i]) noat_frame_destroy(r->frames[i]);
    }
    free(r->frames);

    noat_resources_destroy(r->resources);
    noat_pipeline_destroy(r->pipeline);
    noat_renderpass_destroy(r->render_pass);
    noat_context_destroy(r->context);

    r->initialized = 0;
    LOGI("Paper renderer shutdown");
}