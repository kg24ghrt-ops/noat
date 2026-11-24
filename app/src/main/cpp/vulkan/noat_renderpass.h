#ifndef NOAT_RENDERPASS_H
#define NOAT_RENDERPASS_H

#include "noat_vulkan.h"

NoatRenderPass *noat_renderpass_create(VkDevice device, VkFormat color_format,
                                        VkFormat depth_format);
void noat_renderpass_destroy(NoatRenderPass *rp);
VkRenderPass noat_renderpass_get_handle(const NoatRenderPass *rp);

#endif /* NOAT_RENDERPASS_H */
