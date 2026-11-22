/*
 * noat_vulkan.h - Unified C Vulkan API for noat paper renderer
 * 
 * Opaque handle-based API for Vulkan paper renderer.
 * All objects are opaque pointers managed by create/destroy functions.
 */

#ifndef NOAT_VULKAN_H
#define NOAT_VULKAN_H

#include <vulkan/vulkan.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Forward declarations - opaque handles
 * ======================================================================== */

typedef struct NoatContext NoatContext;
typedef struct NoatRenderPass NoatRenderPass;
typedef struct NoatPipeline NoatPipeline;
typedef struct NoatResources NoatResources;
typedef struct NoatFrame NoatFrame;
typedef struct NoatRenderer NoatRenderer;
typedef struct NoatMaterial NoatMaterial;
typedef struct NoatContext NoatContext;

/* ========================================================================
 * Paper geometry types (from paper_geometry.h)
 * ======================================================================== */

typedef struct {
    const char *id;
    const char *name;
    float width_mm;
    float height_mm;
} NoatPaperSize;

typedef struct {
    float qx;
    float qy;
} NoatPaperQuad;

#define NOAT_PAPER_SCALE 0.85f

/* Paper geometry functions */
const struct NoatPaperSize *noat_paper_default_size(void);
const struct NoatPaperSize *noat_paper_find_size(const char *id);
void noat_paper_compute_quad(const struct NoatPaperSize *size,
                             float surface_width_px,
                             float surface_height_px,
                             float *qx, float *qy);

/* ========================================================================
 * Paper size enumeration
 * ======================================================================== */

typedef enum {
    NOAT_PAPER_A3 = 0,
    NOAT_PAPER_A4 = 1,
    NOAT_PAPER_A5 = 2,
    NOAT_PAPER_A6 = 3,
    NOAT_PAPER_LETTER = 4,
    NOAT_PAPER_LEGAL = 5,
    NOAT_PAPER_COUNT = 6
} NoatPaperType;

/* ========================================================================
 * Light and material parameters
 * ======================================================================== */

typedef struct {
    float direction[3];
    float color[3];
    float intensity;
    float ambient;
    float env_color[3];
    float env_intensity;
} NoatLightParams;

typedef struct {
    float color[3];
    float roughness;
    float specular_strength;
    float specular_roughness;
    float fiber_strength;
    float grain_scale;
    float thickness;
} NoatMaterialParams;

/* ========================================================================
 * Quality levels
 * ======================================================================== */

typedef enum {
    NOAT_QUALITY_LOW = 0,
    NOAT_QUALITY_BALANCED = 1,
    NOAT_QUALITY_HIGH = 2,
    NOAT_QUALITY_ULTRA = 3
} NoatQualityLevel;

/* ========================================================================
 * Context - Vulkan instance, device, swapchain, surface
 * ======================================================================== */

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
const VkSurfaceFormatKHR *noat_context_get_surface_format(const NoatContext *ctx);
VkSurfaceKHR noat_context_get_surface(const NoatContext *ctx);
VkInstance noat_context_get_instance(const NoatContext *ctx);
int noat_context_get_image_count(const NoatContext *ctx);
VkSurfaceFormatKHR noat_context_get_surface_format(const NoatContext *ctx);
VkSwapchainKHR noat_context_get_swapchain(const NoatContext *ctx);
uint32_t noat_context_get_image_count(const NoatContext *ctx);
const VkImageView *noat_context_get_swapchain_image_views(const NoatContext *ctx, uint32_t *count);
VkFormat noat_context_get_swapchain_format(const NoatContext *ctx);
VkExtent2D noat_context_get_extent(const NoatContext *ctx);

/* ========================================================================
 * Render Pass
 * ========================================================================

NoatRenderPass *noat_renderpass_create(VkDevice device, VkFormat color_format);
void noat_renderpass_destroy(NoatRenderPass *rp);
VkRenderPass noat_renderpass_get_handle(const NoatRenderPass *rp);

/* ========================================================================
 * Pipeline
 * ========================================================================

typedef struct {
    float qx;
    float qy;
} NoatPushConstants;

NoatPipeline *noat_pipeline_create(VkDevice device, VkRenderPass render_pass,
                                    VkExtent2D extent, VkDescriptorSetLayout desc_layout);
void noat_pipeline_destroy(NoatPipeline *pl);
VkPipeline noat_pipeline_get_handle(const NoatPipeline *pl);
VkPipelineLayout noat_pipeline_get_layout(const NoatPipeline *pl);

/* Push constants for the vertex shader */
typedef struct {
    float qx;
    float qy;
} NoatPushConstants;

/* ========================================================================
 * Resources (descriptor sets, uniform buffers, samplers)
 * ========================================================================

NoatResources *noat_resources_create(VkDevice device, VkPhysicalDevice physical_device,
                                      VkCommandPool command_pool, VkQueue queue,
                                      uint32_t image_count);
void noat_resources_destroy(NoatResources *res);

VkDescriptorSetLayout noat_resources_get_descriptor_set_layout(const NoatResources *res);
const VkDescriptorSet *noat_resources_get_descriptor_sets(const NoatResources *res, uint32_t *count);
void noat_resources_update_uniform_buffer(NoatResources *res, uint32_t frame_index,
                                          const void *data, size_t size);
VkDescriptorSetLayout noat_resources_get_descriptor_set_layout(const NoatResources *res);

/* ========================================================================
 * Frame (command buffer, sync objects)
 * ========================================================================

NoatFrame *noat_frame_create(VkDevice device, uint32_t queue_family);
void noat_frame_destroy(NoatFrame *frame);

VkCommandBuffer noat_frame_get_command_buffer(const NoatFrame *frame);
VkSemaphore noat_frame_get_image_available_semaphore(const NoatFrame *frame);
VkSemaphore noat_frame_get_render_finished_semaphore(const NoatFrame *frame);
VkFence noat_frame_get_in_flight_fence(const NoatFrame *frame);

void noat_frame_begin_command_buffer(NoatFrame *frame);
void noat_frame_end_command_buffer(NoatFrame *frame);
VkFence noat_frame_get_in_flight_fence(const NoatFrame *frame);
VkSemaphore noat_frame_get_image_available_semaphore(const NoatFrame *frame);
VkSemaphore noat_frame_get_render_finished_semaphore(const NoatFrame *frame);

/* ========================================================================
 * Resources (descriptor sets, uniform buffers, samplers)
 * ========================================================================

NoatResources *noat_resources_create(VkDevice device, VkPhysicalDevice physical_device,
                                      VkCommandPool command_pool, VkQueue queue,
                                      uint32_t swapchain_image_count);
void noat_resources_destroy(NoatResources *res);

VkDescriptorSetLayout noat_resources_get_descriptor_set_layout(const NoatResources *res);
const VkDescriptorSet *noat_resources_get_descriptor_sets(const NoatResources *res, uint32_t *count);
void noat_resources_update_uniform_buffer(NoatResources *res, uint32_t frame_index,
                                          const void *data, size_t size);
VkDescriptorSetLayout noat_resources_get_descriptor_set_layout(const NoatResources *res);

/* ========================================================================
 * Frame (command buffer, sync objects)
 * ========================================================================

NoatFrame *noat_frame_create(VkDevice device, uint32_t queue_family);
void noat_frame_destroy(NoatFrame *frame);

VkCommandBuffer noat_frame_get_command_buffer(const NoatFrame *frame);
VkSemaphore noat_frame_get_image_available_semaphore(const NoatFrame *frame);
VkSemaphore noat_frame_get_render_finished_semaphore(const NoatFrame *frame);
VkFence noat_frame_get_in_flight_fence(const NoatFrame *frame);

void noat_frame_begin_command_buffer(NoatFrame *frame);
void noat_frame_end_command_buffer(NoatFrame *frame);

/* ========================================================================
 * Renderer - main entry point
 * ========================================================================

typedef struct {
    int width;
    int height;
} NoatExtent;

NoatRenderer *noat_renderer_create(void);
void noat_renderer_destroy(NoatRenderer *renderer);

int noat_renderer_init(NoatRenderer *renderer, ANativeWindow *window);
void noat_renderer_resize(NoatRenderer *renderer, int width, int height);
void noat_renderer_render_frame(NoatRenderer *renderer);
void noat_renderer_set_paper_size(NoatRenderer *renderer, const char *size_id);
void noat_renderer_set_material(NoatRenderer *renderer, const NoatMaterialParams *material);
void noat_renderer_set_light(NoatRenderer *renderer, const NoatLightParams *light);
void noat_renderer_set_quality(NoatRenderer *renderer, int quality);
void noat_renderer_shutdown(NoatRenderer *renderer);

const NoatExtent *noat_renderer_get_extent(const NoatRenderer *renderer);
const NoatMaterialParams *noat_renderer_get_material(const NoatRenderer *renderer);
const NoatLightParams *noat_renderer_get_light(const NoatRenderer *renderer);

#ifdef __cplusplus
}
#endif

#endif /* NOAT_VULKAN_H */