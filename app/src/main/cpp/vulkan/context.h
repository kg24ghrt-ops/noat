#pragma once

#include <vulkan/vulkan.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <memory>
#include <vector>
#include <string>

// Vulkan context: instance, physical device, logical device, queues, surface
class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    // Initialize Vulkan with an Android native window
    bool init(ANativeWindow* window);

    // Recreate swapchain on surface size change
    bool resize(int width, int height);

    void shutdown();

    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkDevice device() const { return device_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    VkSurfaceKHR surface() const { return surface_; }
    VkSwapchainKHR swapchain() const { return swapchain_.swapchain; }
    VkFormat swapchainFormat() const { return swapchain_.format; }
    int width() const { return swapchain_.width; }
    int height() const { return swapchain_.height; }
    uint32_t graphicsQueueFamily() const { return graphicsQueueFamily_; }

    // True if VK_LAYER_KHRONOS_validation was enabled on instance or device
    bool validationLayersActive() const { return validationLayersActive_; }

    // Swapchain images and views
    const std::vector<VkImage>& swapchainImages() const { return swapchain_.images; }
    const std::vector<VkImageView>& swapchainImageViews() const { return swapchain_.imageViews; }

private:
    struct SwapchainData {
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        std::vector<VkImage> images;
        std::vector<VkImageView> imageViews;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent = {0, 0};
        int width = 0;
        int height = 0;
    };

    bool createInstance();
    bool createSurface(ANativeWindow* window);
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createSwapchain();
    bool createSwapchainImageViews();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = UINT32_MAX;

    SwapchainData swapchain_;

    bool initialized_ = false;
    bool validationLayersActive_ = false;
};
