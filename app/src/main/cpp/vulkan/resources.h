#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

// Vulkan resources: descriptor sets, uniform buffers, textures
class VulkanResources {
public:
    VulkanResources() = default;
    ~VulkanResources();

    VulkanResources(const VulkanResources&) = delete;
    VulkanResources& operator=(const VulkanResources&) = delete;

    bool init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool,
              VkQueue queue, VkDescriptorSetLayout descriptorSetLayout,
              uint32_t swapchainImageCount);
    void shutdown();

    VkDescriptorSetLayout descriptorSetLayout() const { return descriptorSetLayout_; }
    VkDescriptorPool descriptorPool() const { return descriptorPool_; }
    const std::vector<VkDescriptorSet>& descriptorSets() const { return descriptorSets_; }

    // Update uniform buffer for a specific frame
    void updateUniformBuffer(uint32_t frameIndex, const void* data, VkDeviceSize size);

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;

    // Uniform buffers (one per swapchain image)
    std::vector<VkBuffer> uniformBuffers_;
    std::vector<VkDeviceMemory> uniformBufferMemories_;

    bool createDescriptorSetLayout();
    bool createDescriptorPool(uint32_t maxSets);
    bool createDescriptorSets(uint32_t count);
    bool createUniformBuffers(VkDeviceSize bufferSize);

    uint32_t swapchainImageCount_ = 0;
};
