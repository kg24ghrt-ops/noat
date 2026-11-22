#pragma once

#include <vulkan/vulkan.h>
#include <vector>

// Push constants sent to the vertex shader each frame. Layout must match the
// `PushConstants` block in paper_quad.vert exactly (std140 scalar order).
struct PushConstantBlock {
    float qx;   /* normalised half-width  of the paper quad (NDC) */
    float qy;   /* normalised half-height of the paper quad (NDC) */
};

// Graphics pipeline for the paper renderer.
// Shader SPIR-V is compiled from GLSL (shaders/paper_quad.vert, shaders/paper_quad.frag)
// with glslangValidator and embedded as byte arrays.
class VulkanPipeline {
public:
    VulkanPipeline() = default;
    ~VulkanPipeline();

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    bool init(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
              VkDescriptorSetLayout descriptorSetLayout);
    void shutdown();

    VkPipeline pipeline() const { return pipeline_; }
    VkPipelineLayout pipelineLayout() const { return pipelineLayout_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;

    VkShaderModule createShaderModule(const std::vector<uint32_t>& code);
};
