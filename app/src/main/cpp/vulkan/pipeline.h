#pragma once

#include <vulkan/vulkan.h>
#include <vector>

// Graphics pipeline for the paper renderer.
// Shader SPIR-V is compiled from GLSL (shaders/triangle.vert, shaders/paper_geom.frag)
// with glslangValidator and embedded as byte arrays.
class VulkanPipeline {
public:
    VulkanPipeline() = default;
    ~VulkanPipeline();

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    bool init(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
              VkPipelineLayout pipelineLayout);
    void shutdown();

    VkPipeline pipeline() const { return pipeline_; }
    VkPipelineLayout pipelineLayout() const { return pipelineLayout_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;

    VkShaderModule createShaderModule(const std::vector<uint32_t>& code);
};
