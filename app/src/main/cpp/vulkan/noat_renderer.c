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
    LOGI("Vulkan context initialized, format=%d, size=%dx%d, imageCount=%u",
         r->context->swapchain_format, r->context->width, r->context->height,
         r->context->image_count);

    r->render_pass = noat_renderpass_create(r->context->device, r->context->swapchain_format,
                                             VK_FORMAT_D32_SFLOAT);
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

    // Depth image for the render pass
    VkImageCreateInfo depth_image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .extent = {
            .width = r->context->extent.width,
            .height = r->context->extent.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(r->context->device, &depth_image_info, NULL, &r->depth_image) != VK_SUCCESS) {
        LOGE("Failed to create depth image");
        return 0;
    }

    VkMemoryRequirements depth_mem_req;
    vkGetImageMemoryRequirements(r->context->device, r->depth_image, &depth_mem_req);
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(r->context->physical_device, &mem_props);
    uint32_t depth_mem_type = UINT32_MAX;
    for (uint32_t i = 0; i < 32; i++) {
        if ((depth_mem_req.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            depth_mem_type = i;
            break;
        }
    }
    if (depth_mem_type == UINT32_MAX) {
        LOGE("Failed to find suitable memory type for depth image");
        return 0;
    }
    if (vkAllocateMemory(r->context->device, &(VkMemoryAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = depth_mem_req.size,
        .memoryTypeIndex = depth_mem_type,
    }, NULL, &r->depth_memory) != VK_SUCCESS) {
        LOGE("Failed to allocate depth image memory");
        return 0;
    }
    vkBindImageMemory(r->context->device, r->depth_image, r->depth_memory, 0);

    VkImageViewCreateInfo depth_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = r->depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    if (vkCreateImageView(r->context->device, &depth_view_info, NULL, &r->depth_view) != VK_SUCCESS) {
        LOGE("Failed to create depth image view");
        return 0;
    }
    LOGI("Depth image created: %dx%d", r->context->extent.width, r->context->extent.height);

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
    r->initialized = 1;
    LOGI("Paper renderer initialized successfully, default size=%s (%.0fx%.0f mm)",
         r->current_size->name, r->current_size->width_mm, r->current_size->height_mm);
    return 1;
}

void noat_renderer_resize(NoatRenderer *r, int width, int height) {
    if (!r || !r->initialized) return;
    if (width <= 0 || height <= 0) return;
    if (!noat_context_resize(r->context, width, height)) return;

    LOGI("Paper renderer surface %dx%d", width, height);

    // Destroy old depth resources (extent changed)
    if (r->depth_view) vkDestroyImageView(r->context->device, r->depth_view, NULL);
    if (r->depth_image) vkDestroyImage(r->context->device, r->depth_image, NULL);
    if (r->depth_memory) vkFreeMemory(r->context->device, r->depth_memory, NULL);
    r->depth_view = VK_NULL_HANDLE;
    r->depth_image = VK_NULL_HANDLE;
    r->depth_memory = VK_NULL_HANDLE;

    // Recreate depth image with new extent
    VkImageCreateInfo depth_image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .extent = {
            .width = r->context->extent.width,
            .height = r->context->extent.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(r->context->device, &depth_image_info, NULL, &r->depth_image) != VK_SUCCESS) {
        LOGE("Failed to recreate depth image on resize");
        return;
    }

    VkMemoryRequirements depth_mem_req;
    vkGetImageMemoryRequirements(r->context->device, r->depth_image, &depth_mem_req);
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(r->context->physical_device, &mem_props);
    uint32_t depth_mem_type = UINT32_MAX;
    for (uint32_t i = 0; i < 32; i++) {
        if ((depth_mem_req.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            depth_mem_type = i;
            break;
        }
    }
    if (depth_mem_type == UINT32_MAX) {
        LOGE("Failed to find suitable memory type for depth image on resize");
        return;
    }
    if (vkAllocateMemory(r->context->device, &(VkMemoryAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = depth_mem_req.size,
        .memoryTypeIndex = depth_mem_type,
    }, NULL, &r->depth_memory) != VK_SUCCESS) {
        LOGE("Failed to allocate depth image memory on resize");
        return;
    }
    vkBindImageMemory(r->context->device, r->depth_image, r->depth_memory, 0);

    VkImageViewCreateInfo depth_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = r->depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    if (vkCreateImageView(r->context->device, &depth_view_info, NULL, &r->depth_view) != VK_SUCCESS) {
        LOGE("Failed to recreate depth image view on resize");
        return;
    }

    // Recreate pipeline with new extent
    noat_pipeline_destroy(r->pipeline);
    r->pipeline = noat_pipeline_create(r->context->device, r->render_pass->render_pass,
                                        (VkExtent2D){r->context->width, r->context->height},
                                        r->resources->descriptor_set_layout);
    if (!r->pipeline) {
        LOGE("Failed to recreate pipeline on resize");
        return;
    }

    LOGI("Resize complete: %dx%d, paper %s (%.0fx%.0f mm)",
         r->context->width, r->context->height,
         r->current_size ? r->current_size->name : "(none)",
         r->current_size ? r->current_size->width_mm : 0.0f,
         r->current_size ? r->current_size->height_mm : 0.0f);
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
                                             VK_NULL_HANDLE, &image_index);
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

    VkFence inflight_fence = noat_frame_get_in_flight_fence(r->frames[current_frame]);
    vkResetFences(device, 1, &inflight_fence);

    // Record command buffer
    noat_frame_begin_command_buffer(r->frames[current_frame]);
    VkCommandBuffer cmd = noat_frame_get_command_buffer(r->frames[current_frame]);

    // Create framebuffer with color + depth attachments
    VkImageView attachments[2] = {
        r->context->image_views[image_index],
        r->depth_view,
    };
    VkFramebufferCreateInfo fb_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = r->render_pass->render_pass,
        .attachmentCount = 2,
        .pAttachments = attachments,
        .width = r->context->width,
        .height = r->context->height,
        .layers = 1,
    };
    if (vkCreateFramebuffer(r->context->device, &fb_info, NULL, &framebuffer) != VK_SUCCESS) {
        LOGE("Failed to create framebuffer");
        noat_frame_end_command_buffer(r->frames[current_frame]);
        return;
    }

    VkClearValue clear_values[2] = {
        {{0.0f, 0.0f, 0.0f, 1.0f}},
        {{1.0f, 0}},
    };
    VkRenderPassBeginInfo rp_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = r->render_pass->render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {r->context->width, r->context->height}},
        .clearValueCount = 2,
        .pClearValues = clear_values,
    };
    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

    // Push constants
    NoatPushConstants pc = {0};
    if (r->current_size) {
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
        .width = (float)r->context->width,
        .height = (float)r->context->height,
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
    VkSemaphore wait_semaphore = noat_frame_get_image_available_semaphore(r->frames[current_frame]);
    VkSemaphore signal_semaphore = noat_frame_get_render_finished_semaphore(r->frames[current_frame]);
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &wait_semaphore,
        .pWaitDstStageMask = (VkPipelineStageFlags[]){VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &signal_semaphore,
    };

    if (vkQueueSubmit(r->context->graphics_queue, 1, &submit,
                      noat_frame_get_in_flight_fence(r->frames[current_frame])) != VK_SUCCESS) {
        LOGE("Failed to submit draw command buffer");
        vkDestroyFramebuffer(device, framebuffer, NULL);
        return;
    }

    // Present
    VkSemaphore present_semaphore = noat_frame_get_render_finished_semaphore(r->frames[current_frame]);
    VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &present_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &r->context->swapchain,
        .pImageIndices = &image_index,
    };

    VkResult present_result = vkQueuePresentKHR(r->context->graphics_queue, &present);
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
    const NoatPaperSize *size = noat_paper_find_size(size_id);
    if (!size) {
        LOGE("Unknown paper size id: %s", size_id);
        return;
    }
    r->current_size = size;
    LOGI("Paper size set to %s (%s), %.0fx%.0f mm", size_id, size->name,
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

    if (r->depth_view) vkDestroyImageView(r->context->device, r->depth_view, NULL);
    if (r->depth_image) vkDestroyImage(r->context->device, r->depth_image, NULL);
    if (r->depth_memory) vkFreeMemory(r->context->device, r->depth_memory, NULL);

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