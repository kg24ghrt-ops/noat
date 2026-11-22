#ifndef NOAT_CONTEXT_H
#define NOAT_CONTEXT_H

#include <vulkan/vulkan.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NoatContext NoatContext;

typedef struct NoatContextConfig {
    ANativeWindow *window;
    int width;
    int height;
    int min_image_count;
    int enable_validation;
} NoatContextConfig;

NoatContext *noat_context_create(const NoatContextConfig *config);
void noat_context_destroy(NoatContext *ctx);

VkDevice noat_context_get_device(const NoatContext *ctx);
VkPhysicalDevice noat_context_get_physical_device(const NoatContext *ctx);
VkQueue noat_context_get_graphics_queue(const NoatContext *ctx);
uint32_t noat_context_get_graphics_queue_family(const NoatContext *ctx);
VkFormat noat_context_get_swapchain_format(const NoatContext *ctx);
VkExtent2D noat_context_get_extent(const NoatContext *ctx);
VkSwapchainKHR noat_context_get_swapchain(const NoatContext *ctx);
const VkImageView *noat_context_get_swapchain_image_views(const NoatContext *ctx, uint32_t *count);
int noat_context_get_width(const NoatContext *ctx);
int noat_context_get_height(const NoatContext *ctx);
VkSurfaceKHR noat_context_get_surface(const NoatContext *ctx);
VkInstance noat_context_get_instance(const NoatContext *ctx);
int noat_context_get_image_count(const NoatContext *ctx);
int noat_context_resize(NoatContext *ctx, int width, int height);

#ifdef __cplusplus
}
#endif

#endif /* NOAT_CONTEXT_H */