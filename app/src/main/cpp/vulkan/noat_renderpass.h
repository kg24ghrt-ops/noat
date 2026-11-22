#ifndef NOAT_RENDERPASS_H
#define NOAT_RENDERPASS_H

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NoatRenderPass NoatRenderPass;

NoatRenderPass *noat_renderpass_create(VkDevice device, VkFormat color_format);
void noat_renderpass_destroy(NoatRenderPass *rp);
VkRenderPass noat_renderpass_get_handle(const NoatRenderPass *rp);

#ifdef __cplusplus
}
#endif

#endif /* NOAT_RENDERPASS_H */