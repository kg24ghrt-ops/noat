#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

namespace noat {

class AppRenderer {
public:
    AppRenderer();
    ~AppRenderer();

    AppRenderer(const AppRenderer&) = delete;
    AppRenderer& operator=(const AppRenderer&) = delete;

    // Initialize Vulkan instance, device, VMA allocator (no surface yet).
    bool init();
    // Create surface from ANativeWindow and build swapchain/render resources.
    bool createSurface(ANativeWindow* window);
    bool resize(uint32_t width, uint32_t height);
    // Render one frame and present.
    bool renderFrame();
    // Release surface/swapchain resources (keep instance/device for reuse).
    void releaseSurface();
    // Full shutdown.
    void shutdown();

    const char* info() const;

    // Upload parameter block to the VMA uniform buffer.
    void updateParams(const void* data, size_t size);

private:
    bool createInstance();
    bool selectPhysicalDevice();
    bool createDevice();
    bool createAllocator();
    bool createRenderResourcesOnce();
    bool createSwapchain();
    bool createSyncObjects();
    void destroySwapchain();
    void destroyRenderResources();
    void destroySyncObjects();

    VkInstance m_instance{VK_NULL_HANDLE};
    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    VkQueue m_queue{VK_NULL_HANDLE};
    uint32_t m_queueFamily{UINT32_MAX};
    VkSurfaceKHR m_surface{VK_NULL_HANDLE};
    VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
    VkFormat m_surfaceFormat{VK_FORMAT_UNDEFINED};
    VkExtent2D m_surfaceExtent{};
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    std::vector<VkFramebuffer> m_swapchainFramebuffers;
    VmaAllocator m_allocator{VK_NULL_HANDLE};

    VkRenderPass m_renderPass{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
    VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};
    VkBuffer m_paramsBuffer{VK_NULL_HANDLE};
    VmaAllocation m_paramsAlloc{VK_NULL_HANDLE};

    VkCommandPool m_commandPool{VK_NULL_HANDLE};
    VkCommandBuffer m_commandBuffer{VK_NULL_HANDLE};

    VkSemaphore m_imageAvailableSemaphore{VK_NULL_HANDLE};
    VkSemaphore m_renderFinishedSemaphore{VK_NULL_HANDLE};
    VkFence m_inFlightFence{VK_NULL_HANDLE};

    uint32_t m_width{0};
    uint32_t m_height{0};
    bool m_surfaceCreated{false};
    bool m_swapchainCreated{false};
};

}
