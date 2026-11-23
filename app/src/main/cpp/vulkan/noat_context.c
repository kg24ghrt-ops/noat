/*
 * noat_context.c - Vulkan context management
 */

#include "noat_vulkan.h"
#include "noat_context.h"
#include "noat_renderpass.h"
#include "noat_pipeline.h"
#include "noat_resources.h"
#include "noat_frame.h"

#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_android.h>

#define TAG "NoatContext"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

static const char *kInstanceExtensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
};
#define INSTANCE_EXT_COUNT 2

static const char *kDeviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};
#define DEVICE_EXT_COUNT 1

static const char *kValidationLayers[] = {
    "VK_LAYER_KHRONOS_validation",
};
#define VALIDATION_LAYER_COUNT 1

static VkBool32 debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                               VkDebugUtilsMessageTypeFlagsEXT type,
                               const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
                               void *user_data) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        LOGE("Vulkan Validation: %s", callback_data->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOGW("Vulkan Validation: %s", callback_data->pMessage);
    } else {
        LOGI("Vulkan Validation: %s", callback_data->pMessage);
    }
    return VK_FALSE;
}

static int check_extension_support(const char *req, VkExtensionProperties *avail, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(req, avail[i].extensionName) == 0) {
            return 1;
        }
    }
    return 0;
}

static int create_instance(NoatContext *ctx, int enable_validation) {
    uint32_t ext_count = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &ext_count, NULL);
    VkExtensionProperties *avail_exts = malloc(ext_count * sizeof(VkExtensionProperties));
    vkEnumerateInstanceExtensionProperties(NULL, &ext_count, avail_exts);

    LOGI("Available Vulkan instance extensions:");
    for (uint32_t i = 0; i < ext_count; i++) {
        LOGI("  %s (spec %u)", avail_exts[i].extensionName, avail_exts[i].specVersion);
    }

    const char **enabled_exts = malloc(INSTANCE_EXT_COUNT * sizeof(const char *));
    uint32_t enabled_count = 0;
    for (int i = 0; i < INSTANCE_EXT_COUNT; i++) {
        int found = 0;
        for (uint32_t j = 0; j < ext_count; j++) {
            if (strcmp(kInstanceExtensions[i], avail_exts[j].extensionName) == 0) {
                found = 1;
                break;
            }
        }
        if (found) {
            enabled_exts[enabled_count++] = kInstanceExtensions[i];
        } else {
            LOGW("Extension not available: %s", kInstanceExtensions[i]);
        }
    }

    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "noat";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "paper-engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = 2;
    create_info.ppEnabledExtensionNames = (const char*[]){
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };

#ifndef NDEBUG
    create_info.enabledLayerCount = 1;
    create_info.ppEnabledLayerNames = (const char*[]){"VK_LAYER_KHRONOS_validation"};
#else
    create_info.enabledLayerCount = 0;
#endif

    VkResult result = vkCreateInstance(&create_info, NULL, &ctx->instance);
    free(avail_exts);

    if (result != VK_SUCCESS) {
        LOGE("Failed to create Vulkan instance: %d", result);
        return 0;
    }

    LOGI("Vulkan instance created");
    return 1;
}

static int create_surface(NoatContext *ctx, ANativeWindow *window) {
    VkAndroidSurfaceCreateInfoKHR create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    create_info.window = window;

    VkResult result = vkCreateAndroidSurfaceKHR(ctx->instance, &create_info, NULL, &ctx->surface);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create Android surface: %d", result);
        return 0;
    }
    LOGI("Android surface created");
    return 1;
}

static int pick_physical_device(NoatContext *ctx) {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &device_count, NULL);
    if (device_count == 0) {
        LOGE("No Vulkan physical devices found");
        return 0;
    }

    VkPhysicalDevice *devices = malloc(device_count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(ctx->instance, &device_count, devices);

    for (uint32_t i = 0; i < device_count; i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        LOGI("Physical device: %s (api %d.%d.%d)",
             props.deviceName,
             VK_VERSION_MAJOR(props.apiVersion),
             VK_VERSION_MINOR(props.apiVersion),
             VK_VERSION_PATCH(props.apiVersion));

        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queue_family_count, NULL);
        VkQueueFamilyProperties *queue_families = malloc(queue_family_count * sizeof(VkQueueFamilyProperties));
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queue_family_count, queue_families);

        int graphics_found = 0;
        for (uint32_t i = 0; i < queue_family_count; i++) {
            if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                VkBool32 present_support = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], i, ctx->surface, &present_support);
                if (present_support) {
                    ctx->graphics_queue_family = i;
                    graphics_found = 1;
                    break;
                }
            }
        }
        free(queue_families);
        if (!graphics_found) continue;

        uint32_t ext_count = 0;
        vkEnumerateDeviceExtensionProperties(devices[i], NULL, &ext_count, NULL);
        VkExtensionProperties *avail_exts = malloc(ext_count * sizeof(VkExtensionProperties));
        vkEnumerateDeviceExtensionProperties(devices[i], NULL, &ext_count, avail_exts);

        int has_swapchain = 0;
        for (uint32_t j = 0; j < ext_count; j++) {
            if (strcmp(avail_exts[j].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                has_swapchain = 1;
                break;
            }
        }
        free(avail_exts);
        if (!has_swapchain) {
            free(devices);
            continue;
        }

        ctx->physical_device = devices[i];
        LOGI("Selected physical device: %s", props.deviceName);
        free(devices);
        return 1;
    }

    free(devices);
    LOGE("No suitable Vulkan physical device found");
    return 0;
}

static int create_logical_device(NoatContext *ctx) {
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = ctx->graphics_queue_family,
        .queueCount = 1,
        .pQueuePriorities = (float[]){1.0f},
    };

    VkPhysicalDeviceFeatures features = {0};

    const char *exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pQueueCreateInfos = &queue_info,
        .queueCreateInfoCount = 1,
        .pEnabledFeatures = &(VkPhysicalDeviceFeatures){0},
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = (const char*[]){VK_KHR_SWAPCHAIN_EXTENSION_NAME},
    };

#ifndef NDEBUG
    create_info.enabledLayerCount = 1;
    create_info.ppEnabledLayerNames = (const char*[]){"VK_LAYER_KHRONOS_validation"};
#else
    create_info.enabledLayerCount = 0;
#endif

    VkResult result = vkCreateDevice(ctx->physical_device, &create_info, NULL, &ctx->device);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create logical device: %d", result);
        return 0;
    }

    vkGetDeviceQueue(ctx->device, ctx->graphics_queue_family, 0, &ctx->graphics_queue);
    LOGI("Logical device created");
    return 1;
}

static int create_swapchain(NoatContext *ctx) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical_device, ctx->surface, &caps);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface, &format_count, NULL);
    VkSurfaceFormatKHR *formats = malloc(format_count * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface, &format_count, formats);

    VkSurfaceFormatKHR surface_format = formats[0];
    if (surface_format.format == VK_FORMAT_UNDEFINED) {
        surface_format = (VkSurfaceFormatKHR){VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }
    ctx->surface_format = surface_format;
    free(formats);

    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface, &present_mode_count, NULL);
    VkPresentModeKHR *present_modes = malloc(present_mode_count * sizeof(VkPresentModeKHR));
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface, &present_mode_count, present_modes);
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    free(present_modes);

    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX) {
        ctx->extent = caps.currentExtent;
    } else {
        ctx->extent.width = ctx->width;
        ctx->extent.height = ctx->height;
        ctx->extent.width = ctx->extent.width < caps.minImageExtent.width ? caps.minImageExtent.width :
                           ctx->extent.width > caps.maxImageExtent.width ? caps.maxImageExtent.width : ctx->extent.width;
        ctx->extent.height = ctx->extent.height < caps.minImageExtent.height ? caps.minImageExtent.height :
                            ctx->extent.height > caps.maxImageExtent.height ? caps.maxImageExtent.height : ctx->extent.height;
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = ctx->surface,
        .minImageCount = image_count,
        .imageFormat = ctx->surface_format.format,
        .imageColorSpace = ctx->surface_format.colorSpace,
        .imageExtent = ctx->extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    VkResult result = vkCreateSwapchainKHR(ctx->device, &create_info, NULL, &ctx->swapchain);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create swapchain: %d", result);
        return 0;
    }

    ctx->swapchain_format = ctx->surface_format.format;
    ctx->extent = ctx->extent;

    image_count = 0;
    vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &image_count, NULL);
    ctx->images = malloc(image_count * sizeof(VkImage));
    vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &image_count, ctx->images);
    ctx->image_count = image_count;

    ctx->image_views = malloc(image_count * sizeof(VkImageView));
    for (uint32_t i = 0; i < image_count; i++) {
        VkImageViewCreateInfo iv_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = ctx->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = ctx->swapchain_format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        VkResult result = vkCreateImageView(ctx->device, &iv_info, NULL, &ctx->image_views[i]);
        if (result != VK_SUCCESS) {
            LOGE("Failed to create image view %u: %d", i, result);
            return 0;
        }
    }

    ctx->image_count = image_count;
    LOGI("Swapchain created: %dx%d, format=%d, imageCount=%u",
         ctx->extent.width, ctx->extent.height, ctx->swapchain_format, ctx->image_count);
    return 1;
}

NoatContext *noat_context_create(const NoatContextConfig *config) {
    NoatContext *ctx = calloc(1, sizeof(NoatContext));
    if (!ctx) return NULL;

    ctx->width = config->width;
    ctx->height = config->height;
    ctx->min_image_count = config->min_image_count;
    ctx->enable_validation = config->enable_validation;

    if (!create_instance(ctx, config->enable_validation)) {
        free(ctx);
        return NULL;
    }

    if (!create_surface(ctx, config->window)) {
        noat_context_destroy(ctx);
        return NULL;
    }

    if (!pick_physical_device(ctx)) {
        noat_context_destroy(ctx);
        return NULL;
    }

    if (!create_logical_device(ctx)) {
        noat_context_destroy(ctx);
        return NULL;
    }

    if (!create_swapchain(ctx)) {
        noat_context_destroy(ctx);
        return NULL;
    }

    ctx->initialized = 1;
    LOGI("Vulkan context initialized");
    return ctx;
}

void noat_context_destroy(NoatContext *ctx) {
    if (!ctx) return;

    if (ctx->device) {
        vkDeviceWaitIdle(ctx->device);

        if (ctx->swapchain) {
            for (uint32_t i = 0; i < ctx->image_count; i++) {
                if (ctx->image_views[i]) vkDestroyImageView(ctx->device, ctx->image_views[i], NULL);
            }
            if (ctx->image_views) free(ctx->image_views);
            if (ctx->images) free(ctx->images);
            vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);
        }
        if (ctx->device) vkDestroyDevice(ctx->device, NULL);
    }
    if (ctx->surface) vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);
    if (ctx->instance) vkDestroyInstance(ctx->instance, NULL);
    free(ctx);
}

VkDevice noat_context_get_device(const NoatContext *ctx) { return ctx->device; }
VkPhysicalDevice noat_context_get_physical_device(const NoatContext *ctx) { return ctx->physical_device; }
VkQueue noat_context_get_graphics_queue(const NoatContext *ctx) { return ctx->graphics_queue; }
uint32_t noat_context_get_graphics_queue_family(const NoatContext *ctx) { return ctx->graphics_queue_family; }
VkFormat noat_context_get_swapchain_format(const NoatContext *ctx) { return ctx->swapchain_format; }
VkExtent2D noat_context_get_extent(const NoatContext *ctx) { return ctx->extent; }
VkSwapchainKHR noat_context_get_swapchain(const NoatContext *ctx) { return ctx->swapchain; }
const VkImageView *noat_context_get_swapchain_image_views(const NoatContext *ctx, uint32_t *count) {
    if (count) *count = ctx->image_count;
    return ctx->image_views;
}
int noat_context_get_width(const NoatContext *ctx) { return ctx->width; }
int noat_context_get_height(const NoatContext *ctx) { return ctx->height; }
const VkSurfaceFormatKHR *noat_context_get_surface_format(const NoatContext *ctx) { return &ctx->surface_format; }
VkSurfaceKHR noat_context_get_surface(const NoatContext *ctx) { return ctx->surface; }
VkInstance noat_context_get_instance(const NoatContext *ctx) { return ctx->instance; }
uint32_t noat_context_get_image_count(const NoatContext *ctx) { return ctx->image_count; }

int noat_context_resize(NoatContext *ctx, int width, int height) {
    if (!ctx || !ctx->initialized) return 0;
    vkDeviceWaitIdle(ctx->device);

    for (uint32_t i = 0; i < ctx->image_count; i++) {
        vkDestroyImageView(ctx->device, ctx->image_views[i], NULL);
    }
    free(ctx->image_views);
    free(ctx->images);
    vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);

    ctx->width = width;
    ctx->height = height;

    if (!create_swapchain(ctx)) {
        return 0;
    }
    LOGI("Context resized to %dx%d", width, height);
    return 1;
}