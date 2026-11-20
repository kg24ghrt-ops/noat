#pragma once

#include <vulkan/vulkan.h>
#include <vector>

// Graphics pipelines for the paper renderer:
//   geometryPipeline - renders paper surface (geometry subpass)
//   lightingPipeline - applies lighting + composite (lighting subpass)
class VulkanPipeline {
public:
    VulkanPipeline() = default;
    ~VulkanPipeline();

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    bool init(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
              VkDescriptorSetLayout descriptorSetLayout);
    void shutdown();

    VkPipeline geometryPipeline() const { return geometryPipeline_; }
    VkPipeline lightingPipeline() const { return lightingPipeline_; }
    VkPipelineLayout pipelineLayout() const { return pipelineLayout_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipeline geometryPipeline_ = VK_NULL_HANDLE;
    VkPipeline lightingPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;

    bool createGraphicsPipeline(VkRenderPass renderPass, uint32_t subpass,
                                VkShaderModule vertModule, VkShaderModule fragModule,
                                VkPipeline& outPipeline);
    VkShaderModule createShaderModule(const std::vector<uint32_t>& code);
};
