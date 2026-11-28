#pragma once

#include <vulkan/vulkan.h>

// ANativeWindow is an Android NDK type; forward-declare here so this header
// compiles on host Linux (C unit tests) as well as on Android (renderer).
struct ANativeWindow;

#ifdef __cplusplus
namespace noat {
#endif

// C-compatible type declarations exposed via libpaper-engine.so dlsym API.
// These mirror the C++ AppRenderer concepts but use plain C structs so
// noat_engine_test.c and noat_vulkan_test.c can dlopen/dlsym them.

struct PaperSize {
    const char* id;
    const char* name;
    float width_mm;
    float height_mm;
};
typedef struct PaperSize PaperSize;

struct PaperQuad {
    float qx;
    float qy;
};
typedef struct PaperQuad PaperQuad;

struct NoatContextConfig {
    uint32_t width;
    uint32_t height;
    VkFormat color_format;
    VkFormat depth_format;
};
typedef struct NoatContextConfig NoatContextConfig;

struct NoatContext {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family;
    VkFormat swapchain_format;
    VkExtent2D extent;
};
typedef struct NoatContext NoatContext;

struct NoatRenderPass {
    VkRenderPass handle;
};
typedef struct NoatRenderPass NoatRenderPass;

struct NoatPipeline {
    VkPipeline handle;
    VkPipelineLayout layout;
};
typedef struct NoatPipeline NoatPipeline;

struct NoatRenderer {
    // Opaque; created via noat_renderer_create.
    void* impl;
};
typedef struct NoatRenderer NoatRenderer;

// C API function pointer-compatible declarations (declared here so test files
// can reference the types; actual symbols are resolved via dlsym).
typedef struct NoatContext* (*noat_context_create_fn)(const struct NoatContextConfig*);
typedef void (*noat_context_destroy_fn)(struct NoatContext*);
typedef VkDevice (*noat_context_get_device_fn)(const struct NoatContext*);
typedef VkPhysicalDevice (*noat_context_get_physical_device_fn)(const struct NoatContext*);
typedef VkQueue (*noat_context_get_graphics_queue_fn)(const struct NoatContext*);
typedef uint32_t (*noat_context_get_graphics_queue_family_fn)(const struct NoatContext*);
typedef VkFormat (*noat_context_get_swapchain_format_fn)(const struct NoatContext*);
typedef VkExtent2D (*noat_context_get_extent_fn)(const struct NoatContext*);
typedef int (*noat_context_get_width_fn)(const struct NoatContext*);
typedef int (*noat_context_get_height_fn)(const struct NoatContext*);
typedef int (*noat_context_resize_fn)(struct NoatContext*, int, int);

typedef struct NoatRenderPass* (*noat_renderpass_create_fn)(VkDevice, VkFormat, VkFormat);
typedef void (*noat_renderpass_destroy_fn)(struct NoatRenderPass*);
typedef VkRenderPass (*noat_renderpass_get_handle_fn)(const struct NoatRenderPass*);

typedef struct NoatPipeline* (*noat_pipeline_create_fn)(VkDevice, VkRenderPass,
                                                        VkExtent2D, VkDescriptorSetLayout);
typedef void (*noat_pipeline_destroy_fn)(struct NoatPipeline*);
typedef VkPipeline (*noat_pipeline_get_handle_fn)(const struct NoatPipeline*);

typedef struct NoatRenderer* (*noat_renderer_create_fn)(void);
typedef void (*noat_renderer_destroy_fn)(struct NoatRenderer*);
typedef int (*noat_renderer_init_fn)(struct NoatRenderer*, struct ANativeWindow*);
typedef void (*noat_renderer_resize_fn)(struct NoatRenderer*, int, int);
typedef void (*noat_renderer_render_frame_fn)(struct NoatRenderer*);
typedef void (*noat_renderer_shutdown_fn)(struct NoatRenderer*);

typedef const struct PaperSize* (*noat_paper_default_size_fn)(void);
typedef const struct PaperSize* (*noat_paper_find_size_fn)(const char*);
typedef void (*noat_paper_compute_quad_fn)(const struct PaperSize*, float, float, float*, float*);

#ifdef __cplusplus
} // namespace noat
#endif
