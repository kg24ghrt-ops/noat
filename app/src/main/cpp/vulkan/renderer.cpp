#include "renderer.h"

#include "context.h"
#include "frame.h"
#include "pipeline.h"
#include "renderpass.h"
#include "resources.h"

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>

#define TAG "PaperRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

namespace {
// One source of truth for paper sizes. Dimensions in mm, portrait orientation.
// These ids/dimensions MUST match the Kotlin PaperSize table in MainActivity.kt.
struct PaperSize {
    const char* id;          // stable id, passed over JNI (not an ordinal)
    const char* name;        // human-readable label shown in the UI
    float widthMm;
    float heightMm;
    float aspect() const { return widthMm / heightMm; }
};

constexpr PaperSize kPaperSizes[] = {
    {"A3",   "A3",   297.0f, 420.0f},
    {"A4",   "A4",   210.0f, 297.0f},
    {"A5",   "A5",   148.0f, 210.0f},
    {"A6",   "A6",   105.0f, 148.0f},
    {"LETTER", "Letter", 216.0f, 279.0f},
    {"LEGAL",  "Legal",  216.0f, 356.0f},
};
constexpr size_t kPaperSizeCount = sizeof(kPaperSizes) / sizeof(kPaperSizes[0]);

const PaperSize* findPaperSize(const char* id) {
    for (size_t i = 0; i < kPaperSizeCount; ++i) {
        if (std::strcmp(kPaperSizes[i].id, id) == 0) {
            return &kPaperSizes[i];
        }
    }
    return nullptr;
}
} // namespace

PaperRenderer::PaperRenderer() = default;

PaperRenderer::~PaperRenderer() {
    shutdown();
}

bool PaperRenderer::init(void* nativeWindow) {
    ANativeWindow* window = static_cast<ANativeWindow*>(nativeWindow);
    LOGI("PaperRenderer::init called");

    context_ = std::make_unique<VulkanContext>();
    if (!context_->init(window)) {
        LOGE("Failed to initialize Vulkan context");
        return false;
    }
    LOGI("Vulkan context initialized, format=%d, size=%dx%d, imageCount=%zu",
         context_->swapchainFormat(), context_->width(), context_->height(),
         context_->swapchainImages().size());
    LOGI("Validation layers active: %s",
         context_->validationLayersActive() ? "true" : "false");

    // Create render pass
    renderPass_ = std::make_unique<VulkanRenderPass>();
    if (!renderPass_->init(context_->device(), context_->swapchainFormat())) {
        LOGE("Failed to create render pass");
        return false;
    }
    LOGI("Render pass created");

    // Create command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = context_->graphicsQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(context_->device(), &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        LOGE("Failed to create command pool");
        return false;
    }
    LOGI("Command pool created");

    // Create resources (includes descriptor set layout)
    resources_ = std::make_unique<VulkanResources>();
    uint32_t imageCount = static_cast<uint32_t>(context_->swapchainImages().size());
    if (!resources_->init(context_->device(), context_->physicalDevice(), commandPool_,
                          context_->graphicsQueue(), imageCount)) {
        LOGE("Failed to create resources");
        return false;
    }
    LOGI("Resources created, descriptorSetLayout=%llu, descriptorSets=%zu",
         reinterpret_cast<unsigned long long>(resources_->descriptorSetLayout()),
         resources_->descriptorSets().size());

    // Create pipeline using the descriptor set layout from resources
    pipeline_ = std::make_unique<VulkanPipeline>();
    VkExtent2D extent = {static_cast<uint32_t>(context_->width()),
                         static_cast<uint32_t>(context_->height())};
    if (!pipeline_->init(context_->device(), renderPass_->renderPass(), extent,
                         resources_->descriptorSetLayout())) {
        LOGE("Failed to create pipeline");
        return false;
    }
    LOGI("Pipeline created, pipeline=%llu, layout=%llu",
         reinterpret_cast<unsigned long long>(pipeline_->pipeline()),
         reinterpret_cast<unsigned long long>(pipeline_->pipelineLayout()));

    frames_.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        frames_[i] = std::make_unique<VulkanFrame>();
        if (!frames_[i]->init(context_->device(), context_->graphicsQueueFamily())) {
            LOGE("Failed to initialize frame %u", i);
            return false;
        }
    }
    LOGI("Frames created (%zu)", frames_.size());
    // Set default lighting
    light_.direction[0] = 0.5f; light_.direction[1] = -1.0f; light_.direction[2] = 0.5f;
    light_.color[0] = 1.0f; light_.color[1] = 1.0f; light_.color[2] = 0.95f;
    light_.intensity = 1.0f;
    light_.ambient = 0.3f;
    light_.envColor[0] = 0.8f; light_.envColor[1] = 0.85f; light_.envColor[2] = 0.9f;
    light_.envIntensity = 0.4f;

    // Set default material: standard notebook paper
    material_.color[0] = 0.92f; material_.color[1] = 0.94f; material_.color[2] = 0.98f;
    material_.roughness = 0.4f;
    material_.specularStrength = 0.1f;
    material_.specularRoughness = 0.6f;
    material_.fiberStrength = 0.5f;
    material_.grainScale = 1.0f;
    material_.thickness = 0.08f;

    initialized_ = true;
    LOGI("Paper renderer initialized successfully");
    return true;
}

bool PaperRenderer::resize(int width, int height) {
    if (!initialized_) return false;
    if (!context_->resize(width, height)) return false;

    surfaceWidth_ = width;
    surfaceHeight_ = height;

    LOGI("Paper renderer surface %dx%d", width, height);
    resources_->shutdown();
    uint32_t imageCount = static_cast<uint32_t>(context_->swapchainImages().size());
    if (!resources_->init(context_->device(), context_->physicalDevice(), commandPool_,
                          context_->graphicsQueue(), imageCount)) {
        LOGE("Failed to recreate resources after resize");
        return false;
    }

    // Recreate frames
    for (auto& f : frames_) {
        f->shutdown();
    }
    frames_.clear();
    frames_.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        frames_[i] = std::make_unique<VulkanFrame>();
        if (!frames_[i]->init(context_->device(), context_->graphicsQueueFamily())) {
            LOGE("Failed to initialize frame %u", i);
            return false;
        }
    }

    LOGI("Paper renderer resized to %dx%d", width, height);
    return true;
}

void PaperRenderer::setConfig(QualityLevel quality) {
    quality_ = quality;
}

void PaperRenderer::setLight(const LightParams& light) {
    light_ = light;
}

void PaperRenderer::setMaterial(const PaperMaterialParams& material) {
    material_ = material;
}

void PaperRenderer::setPaperSize(const char* sizeId) {
    const PaperSize* size = findPaperSize(sizeId);
    if (size == nullptr) {
        LOGE("Unknown paper size id: %s, keeping %s", sizeId, paperSizeId_.c_str());
        return;
    }
    paperSizeId_ = sizeId;
    paperAspect_ = size->aspect();
    LOGI("Paper size set to %s (%s), aspect=%.4f", sizeId, size->name, paperAspect_);
}

void PaperRenderer::renderFrame() {
    if (!initialized_) {
        LOGW("renderFrame called but renderer not initialized");
        return;
    }

    VkDevice device = context_->device();
    VkFramebuffer framebuffer = VK_NULL_HANDLE;

    LOGI("renderFrame #%u: begin", currentFrame_);

    // Wait for previous frame to finish
    VkFence waitFence = frames_[currentFrame_]->inFlightFence();
    VkResult waitResult = vkWaitForFences(device, 1, &waitFence, VK_TRUE, UINT64_MAX);
    if (waitResult != VK_SUCCESS) {
        LOGE("renderFrame #%u: vkWaitForFences failed: %d", currentFrame_, waitResult);
        return;
    }

    // Acquire next swapchain image
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, context_->swapchain(), UINT64_MAX,
                                            frames_[currentFrame_]->imageAvailableSemaphore(),
                                            VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        LOGI("renderFrame #%u: swapchain out of date (%d), resetting fence", currentFrame_, result);
        VkFence resetFence = frames_[currentFrame_]->inFlightFence();
        vkResetFences(device, 1, &resetFence);
        return;
    }
    if (result != VK_SUCCESS) {
        LOGE("renderFrame #%u: vkAcquireNextImageKHR failed: %d", currentFrame_, result);
        return;
    }
    LOGI("renderFrame #%u: acquired image %u", currentFrame_, imageIndex);

    VkFence resetFence2 = frames_[currentFrame_]->inFlightFence();
    vkResetFences(device, 1, &resetFence2);

    // Record command buffer
    if (!frames_[currentFrame_]->beginCommandBuffer()) {
        LOGE("renderFrame #%u: failed to begin command buffer", currentFrame_);
        return;
    }

    VkCommandBuffer cmd = frames_[currentFrame_]->commandBuffer();

    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_->renderPass();

    // Create framebuffer for this image (single color attachment)
    std::vector<VkImageView> attachments = {
        context_->swapchainImageViews()[imageIndex],
    };

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass_->renderPass();
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = context_->width();
    fbInfo.height = context_->height();
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        LOGE("renderFrame #%u: failed to create framebuffer", currentFrame_);
        frames_[currentFrame_]->endCommandBuffer();
        VkFence resetFence3 = frames_[currentFrame_]->inFlightFence();
        vkResetFences(device, 1, &resetFence3);
        return;
    }
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {static_cast<uint32_t>(context_->width()),
                                        static_cast<uint32_t>(context_->height())};

    // Clear values (single color attachment)
    std::array<VkClearValue, 1> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Draw paper (3 vertices for a full-screen triangle, no index buffer needed)
    VkPipeline pipeline = pipeline_->pipeline();
    if (pipeline == VK_NULL_HANDLE) {
        LOGE("renderFrame #%u: Pipeline is null - cannot draw", currentFrame_);
        vkCmdEndRenderPass(cmd);
        frames_[currentFrame_]->endCommandBuffer();
        vkDestroyFramebuffer(device, framebuffer, nullptr);
        VkFence resetFence4 = frames_[currentFrame_]->inFlightFence();
        vkResetFences(device, 1, &resetFence4);
        return;
    }
    LOGI("renderFrame #%u: pipeline bound, drawing 3 vertices", currentFrame_);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Compute paper rect inside the surface, preserving aspect ratio with a margin.
    int sw = (surfaceWidth_ > 0) ? surfaceWidth_ : context_->width();
    int sh = (surfaceHeight_ > 0) ? surfaceHeight_ : context_->height();
    if (sw <= 0 || sh <= 0) {
        sw = context_->width();
        sh = context_->height();
    }

    const float marginFraction = 0.05f; // 5% margin on each side
    float margin = marginFraction * static_cast<float>(std::min(sw, sh));
    float availW = static_cast<float>(sw) - 2.0f * margin;
    float availH = static_cast<float>(sh) - 2.0f * margin;

    float paperW, paperH;
    if (availW / availH > paperAspect_) {
        paperH = availH;
        paperW = paperH * paperAspect_;
    } else {
        paperW = availW;
        paperH = paperW / paperAspect_;
    }

    int x = static_cast<int>((static_cast<float>(sw) - paperW) * 0.5f);
    int y = static_cast<int>((static_cast<float>(sh) - paperH) * 0.5f);
    int w = static_cast<int>(paperW);
    int h = static_cast<int>(paperH);
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    LOGI("renderFrame #%u: paper rect %dx%d at (%d,%d) of %dx%d (aspect=%.4f)",
         currentFrame_, w, h, x, y, sw, sh, paperAspect_);

    VkViewport viewport{};
    viewport.x = static_cast<float>(x);
    viewport.y = static_cast<float>(y);
    viewport.width = static_cast<float>(w);
    viewport.height = static_cast<float>(h);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {x, y};
    scissor.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind descriptor sets
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->pipelineLayout(),
                            0, 1, &resources_->descriptorSets()[currentFrame_], 0, nullptr);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);

    frames_[currentFrame_]->endCommandBuffer();

    // Submit
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    VkSemaphore imageAvailableSemaphore = frames_[currentFrame_]->imageAvailableSemaphore();
    submitInfo.pWaitSemaphores = &imageAvailableSemaphore;
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    VkSemaphore renderFinishedSemaphore = frames_[currentFrame_]->renderFinishedSemaphore();
    submitInfo.pSignalSemaphores = &renderFinishedSemaphore;

    if (vkQueueSubmit(context_->graphicsQueue(), 1, &submitInfo,
                      frames_[currentFrame_]->inFlightFence()) != VK_SUCCESS) {
        LOGE("renderFrame #%u: failed to submit draw command buffer", currentFrame_);
        vkDestroyFramebuffer(device, framebuffer, nullptr);
        return;
    }

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    VkSwapchainKHR swapchain = context_->swapchain();
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr;

    VkResult presentResult = vkQueuePresentKHR(context_->graphicsQueue(), &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        LOGI("renderFrame #%u: present out of date/suboptimal (%d)", currentFrame_, presentResult);
    } else if (presentResult != VK_SUCCESS) {
        LOGE("renderFrame #%u: vkQueuePresentKHR failed: %d", currentFrame_, presentResult);
    } else {
        LOGI("renderFrame #%u: presented successfully", currentFrame_);
    }

    vkDestroyFramebuffer(device, framebuffer, nullptr);

    currentFrame_ = (currentFrame_ + 1) % frames_.size();
}

void PaperRenderer::shutdown() {
    if (!initialized_) return;

    vkDeviceWaitIdle(context_->device());

    for (auto& f : frames_) {
        f->shutdown();
    }
    frames_.clear();

    resources_->shutdown();
    pipeline_->shutdown();
    renderPass_->shutdown();
    context_->shutdown();

    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_->device(), commandPool_, nullptr);
    }

    initialized_ = false;
    LOGI("Paper renderer shutdown");
}
