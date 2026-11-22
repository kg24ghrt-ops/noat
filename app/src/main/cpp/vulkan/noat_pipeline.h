#ifndef NOAT_PIPELINE_H
#define NOAT_PIPELINE_H

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NoatPipeline NoatPipeline;

typedef struct {
    float qx;
    float qy;
} NoatPushConstants;

NoatPipeline *noat_pipeline_create(VkDevice device, VkRenderPass render_pass,
                                    VkExtent2D extent, VkDescriptorSetLayout desc_layout);
void noat_pipeline_destroy(NoatPipeline *pl);
VkPipeline noat_pipeline_get_handle(const NoatPipeline *pl);
VkPipelineLayout noat_pipeline_get_layout(const NoatPipeline *pl);

#ifdef __cplusplus
}
#endif

#endif /* NOAT_PIPELINE_H */