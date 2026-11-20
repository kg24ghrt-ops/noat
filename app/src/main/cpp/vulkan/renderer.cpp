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

#define TAG "PaperRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

PaperRenderer::PaperRenderer() = default;

PaperRenderer::~PaperRenderer() {
    shutdown();
}

bool PaperRenderer::init(void* nativeWindow) {
    ANativeWindow* window = static_cast<ANativeWindow*>(nativeWindow);

    context_ = std::make_unique<VulkanContext>();
    if (!context_->init(window)) {
        LOGE("Failed to initialize Vulkan context");
        return false;
    }

    // Create render pass
    renderPass_ = std::make_unique<VulkanRenderPass>();
    if (!renderPass_->init(context_->device(), VK_FORMAT_B8G8R8A8_UNORM,
                           VK_FORMAT_D32_SFLOAT)) {
        LOGE("Failed to create render pass");
        return false;
    }

    // Create command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = context_->graphicsQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(context_->device(), &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        LOGE("Failed to create command pool");
        return false;
    }

    // Create resources (includes descriptor set layout)
    resources_ = std::make_unique<VulkanResources>();
    uint32_t imageCount = static_cast<uint32_t>(context_->swapchainImages().size());
    if (!resources_->init(context_->device(), context_->physicalDevice(), commandPool_,
                          context_->graphicsQueue(), imageCount)) {
        LOGE("Failed to create resources");
        return false;
    }

    // Create pipeline using the descriptor set layout from resources
    pipeline_ = std::make_unique<VulkanPipeline>();
    VkExtent2D extent = {static_cast<uint32_t>(context_->width()),
                         static_cast<uint32_t>(context_->height())};
    if (!pipeline_->init(context_->device(), renderPass_->renderPass(), extent,
                         resources_->descriptorSetLayout())) {
        LOGE("Failed to create pipeline");
        return false;
    }

    // Create frames (one per swapchain image)
    frames_.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        frames_[i] = std::make_unique<VulkanFrame>();
        if (!frames_[i]->init(context_->device(), context_->graphicsQueueFamily())) {
            LOGE("Failed to initialize frame %u", i);
            return false;
        }
    }
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
    LOGI("Paper renderer initialized");
    return true;
}

bool PaperRenderer::resize(int width, int height) {
    if (!initialized_) return false;
    if (!context_->resize(width, height)) return false;

    // Recreate resources that depend on swapchain image count
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

void PaperRenderer::renderFrame() {
    if (!initialized_) return;

    VkDevice device = context_->device();
    VkFramebuffer framebuffer = VK_NULL_HANDLE;

    // Wait for previous frame to finish
    VkFence waitFence = frames_[currentFrame_]->inFlightFence();
    vkWaitForFences(device, 1, &waitFence, VK_TRUE, UINT64_MAX);

    // Acquire next swapchain image
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, context_->swapchain(), UINT64_MAX,
                                            frames_[currentFrame_]->imageAvailableSemaphore(),
                                            VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Handle swapchain out of date (should call resize, but we don't have dimensions)
        VkFence resetFence = frames_[currentFrame_]->inFlightFence();
        vkResetFences(device, 1, &resetFence);
        return;
    }

    VkFence resetFence2 = frames_[currentFrame_]->inFlightFence();
    vkResetFences(device, 1, &resetFence2);

    // Record command buffer
    frames_[currentFrame_]->beginCommandBuffer();

    VkCommandBuffer cmd = frames_[currentFrame_]->commandBuffer();

    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_->renderPass();

    // Create framebuffer for this image (color + normal + depth + swapchain)
    // For simplicity, create framebuffers lazily here
    std::vector<VkImageView> attachments = {
        // We'll use swapchain image view as both color input and final output
        // In a full implementation, we'd have separate G-buffer attachments
        context_->swapchainImageViews()[imageIndex],
        context_->swapchainImageViews()[imageIndex],
        context_->swapchainImageViews()[imageIndex],
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
        LOGE("Failed to create framebuffer");
        frames_[currentFrame_]->endCommandBuffer();
        VkFence resetFence3 = frames_[currentFrame_]->inFlightFence();
        vkResetFences(device, 1, &resetFence3);
        return;
    }
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {static_cast<uint32_t>(context_->width()),
                                        static_cast<uint32_t>(context_->height())};

    // Clear values
    std::array<VkClearValue, 4> clearValues{};
    clearValues[0].color = {{0.95f, 0.95f, 0.90f, 1.0f}};
    clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clearValues[2].depthStencil = {1.0f, 0};
    clearValues[3].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Geometry subpass (subpass 0)
    if (pipeline_->geometryPipeline() != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->geometryPipeline());

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(context_->width());
        viewport.height = static_cast<float>(context_->height());
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = renderPassInfo.renderArea.extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Bind descriptor sets
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->pipelineLayout(),
                                0, 1, &resources_->descriptorSets()[currentFrame_], 0, nullptr);

        // Draw paper (3 vertices for a full-screen triangle, no index buffer needed)
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    // Next subpass: Lighting (subpass 1)
    vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);

    if (pipeline_->lightingPipeline() != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->lightingPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->pipelineLayout(),
                                0, 1, &resources_->descriptorSets()[currentFrame_], 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

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
        LOGE("Failed to submit draw command buffer");
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

    vkQueuePresentKHR(context_->graphicsQueue(), &presentInfo);

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
