#ifndef NOAT_RESOURCES_H
#define NOAT_RESOURCES_H

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NoatResources NoatResources;

NoatResources *noat_resources_create(VkDevice device, VkPhysicalDevice physical_device,
                                      VkCommandPool command_pool, VkQueue queue,
                                      uint32_t swapchain_image_count);
void noat_resources_destroy(NoatResources *res);

VkDescriptorSetLayout noat_resources_get_descriptor_set_layout(const NoatResources *res);
const VkDescriptorSet *noat_resources_get_descriptor_sets(const NoatResources *res, uint32_t *count);
void noat_resources_update_uniform_buffer(NoatResources *res, uint32_t frame_index,
                                          const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* NOAT_RESOURCES_H */