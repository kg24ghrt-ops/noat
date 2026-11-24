/*
 * noat_vulkan_test.c - Standalone Vulkan rendering test on actual device
 *
 * Validates the noat Vulkan engine rendering path on Mali-G610 MC6.
 * Uses offscreen rendering (no ANativeWindow/swapchain needed).
 *
 * Build:
 *   clang -std=c11 -Wall -Wextra -I app/src/main/cpp/vulkan \
 *       noat_vulkan_test.c app/src/main/cpp/vulkan/noat_context.c \
 *       app/src/main/cpp/vulkan/noat_renderpass.c \
 *       app/src/main/cpp/vulkan/noat_pipeline.c \
 *       app/src/main/cpp/vulkan/noat_resources.c \
 *       app/src/main/cpp/vulkan/noat_frame.c \
 *       app/src/main/cpp/vulkan/noat_paper_geometry.c \
 *       app/src/main/cpp/vulkan/noat_renderer.c \
 *       -L/system/lib64 -lvulkan -llog -landroid -o noat_vulkan_test
 *
 * Run:
 *   ./noat_vulkan_test
 */

#include "noat_vulkan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOGI(...) printf("[INFO] " __VA_ARGS__)
#define LOGE(...) fprintf(stderr, "[ERROR] " __VA_ARGS__)
#define LOGW(...) printf("[WARN] " __VA_ARGS__)

static const char *kResultStrings[] = {
    "VK_SUCCESS", "NOT_READY", "TIMEOUT", "EVENT_SET", "EVENT_RESET",
    "INCOMPLETE", "ERROR_OUT_OF_HOST_MEMORY", "ERROR_OUT_OF_DEVICE_MEMORY",
    "ERROR_INITIALIZATION_FAILED", "ERROR_DEVICE_LOST", "ERROR_MEMORY_MAP_FAILED",
    "ERROR_LAYER_NOT_PRESENT", "ERROR_EXTENSION_NOT_PRESENT",
    "ERROR_FEATURE_NOT_PRESENT", "ERROR_INCOMPATIBLE_DRIVER", "ERROR_TOO_MANY_OBJECTS",
    "ERROR_FORMAT_NOT_SUPPORTED", "ERROR_SURFACE_LOST_KHR",
    "ERROR_NATIVE_WINDOW_IN_USE_KHR", "SUBOPTIMAL_KHR", "ERROR_OUT_OF_DATE_KHR",
    "ERROR_INCOMPATIBLE_DISPLAY_KHR", "ERROR_VALIDATION_FAILED_EXT",
    "ERROR_INVALID_SHADER_NV",
};

static const char *vk_result_str(VkResult r) {
    int idx = (int)(-r);
    if (idx >= 0 && idx < (int)(sizeof(kResultStrings)/sizeof(kResultStrings[0])))
        return kResultStrings[idx];
    return "UNKNOWN";
}

static VkBool32 debug_callback(VkDebugReportFlagsEXT flags,
                                VkDebugReportObjectTypeEXT objType,
                                uint64_t obj, size_t location,
                                int32_t code, const char *layerPrefix,
                                const char *msg, void *userData) {
    (void)flags; (void)objType; (void)obj; (void)location; (void)code;
    (void)layerPrefix; (void)userData;
    LOGW("VALIDATION: %s\n", msg);
    return VK_FALSE;
}

int main(void) {
    int failures = 0;
    clock_t t0, t1;

    LOGI("=== NOAT Vulkan Rendering Test ===\n");
    LOGI("Target: Mali-G610 MC6 on Android 15\n\n");

    /* -----------------------------------------------------------
     * Stage A: Create Vulkan instance
     * ----------------------------------------------------------- */
    t0 = clock();

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "noat-vulkan-test",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "noat-paper-engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_1,
    };

    const char *instance_extensions[] = {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
#ifdef VK_EXT_DEBUG_REPORT_EXTENSION_NAME
        VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
#endif
    };

    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = instance_extensions,
    };

#ifdef VK_EXT_DEBUG_REPORT_EXTENSION_NAME
    VkDebugReportCallbackCreateInfoEXT debug_create = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT,
        .flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT,
        .pfnCallback = debug_callback,
    };
    inst_info.pNext = &debug_create;
#endif

    VkInstance instance;
    VkResult res = vkCreateInstance(&inst_info, NULL, &instance);
    if (res != VK_SUCCESS) {
        LOGE("Failed to create Vulkan instance: %s\n", vk_result_str(res));
        return 1;
    }
    LOGI("Vulkan instance created\n");

    /* Debug callback */
#ifdef VK_EXT_DEBUG_REPORT_EXTENSION_NAME
    VkDebugReportCallbackEXT debug_callback_handle;
    PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT =
        (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
    if (vkCreateDebugReportCallbackEXT) {
        vkCreateDebugReportCallbackEXT(instance, &debug_create, NULL, &debug_callback_handle);
    }
#endif

    t1 = clock();
    LOGI("Instance creation: %.2f ms\n", (double)(t1 - t0) * 1000.0 / CLOCKS_PER_SEC);

    /* -----------------------------------------------------------
     * Stage B: Enumerate physical devices
     * ----------------------------------------------------------- */
    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(instance, &gpu_count, NULL);
    if (gpu_count == 0) {
        LOGE("No Vulkan physical devices found\n");
        return 1;
    }

    VkPhysicalDevice *gpus = malloc(gpu_count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(instance, &gpu_count, gpus);

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < gpu_count; i++) {
        VkPhysicalDeviceProperties gpu_props;
        vkGetPhysicalDeviceProperties(gpus[i], &gpu_props);
        LOGI("GPU %u: %s (api %d.%d.%d)\n",
             i, gpu_props.deviceName,
             VK_VERSION_MAJOR(gpu_props.apiVersion),
             VK_VERSION_MINOR(gpu_props.apiVersion),
             VK_VERSION_PATCH(gpu_props.apiVersion));
        if (strstr(gpu_props.deviceName, "Mali-G610") != NULL) {
            chosen = gpus[i];
            LOGI("  --> Selected Mali-G610\n");
        }
    }
    if (chosen == VK_NULL_HANDLE) {
        chosen = gpus[0];
        LOGW("No Mali-G610 found, using first device\n");
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(chosen, &props);
    /* Driver info via properties2 */
    VkPhysicalDeviceDriverProperties driver_props = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
    };
    VkPhysicalDeviceProperties2 props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &driver_props,
    };
    PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2_fn =
        (PFN_vkGetPhysicalDeviceProperties2)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2");
    if (vkGetPhysicalDeviceProperties2_fn) {
        vkGetPhysicalDeviceProperties2_fn(chosen, &props2);
    }
    LOGI("Selected: %s, Vulkan %d.%d.%d, driver %s\n",
         props.deviceName,
         VK_VERSION_MAJOR(props.apiVersion),
         VK_VERSION_MINOR(props.apiVersion),
         VK_VERSION_PATCH(props.apiVersion),
         driver_props.driverInfo);

    /* Queue family */
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(chosen, &qf_count, NULL);
    VkQueueFamilyProperties *qfps = malloc(qf_count * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(chosen, &qf_count, qfps);

    uint32_t graphics_qf = UINT32_MAX;
    for (uint32_t i = 0; i < qf_count; i++) {
        if (qfps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics_qf = i;
            LOGI("Graphics queue family: %u\n", i);
            break;
        }
    }
    if (graphics_qf == UINT32_MAX) {
        LOGE("No graphics queue family found\n");
        return 1;
    }

    /* -----------------------------------------------------------
     * Stage C: Create logical device
     * ----------------------------------------------------------- */
    t0 = clock();
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo qinfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphics_qf,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    const char *device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    VkDeviceCreateInfo dev_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qinfo,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = device_extensions,
        .pEnabledFeatures = &(VkPhysicalDeviceFeatures){0},
    };

    VkDevice device;
    res = vkCreateDevice(chosen, &dev_info, NULL, &device);
    if (res != VK_SUCCESS) {
        LOGE("Failed to create logical device: %s\n", vk_result_str(res));
        return 1;
    }
    LOGI("Logical device created\n");

    VkQueue queue;
    vkGetDeviceQueue(device, graphics_qf, 0, &queue);

    t1 = clock();
    LOGI("Device creation: %.2f ms\n", (double)(t1 - t0) * 1000.0 / CLOCKS_PER_SEC);

    /* -----------------------------------------------------------
     * Stage D: Create offscreen images (color + depth)
     * ----------------------------------------------------------- */
    const uint32_t width = 1080;
    const uint32_t height = 1920;
    const VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
    const VkFormat depth_format = VK_FORMAT_D32_SFLOAT;

    /* Create command pool */
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = graphics_qf,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };
    VkCommandPool command_pool;
    res = vkCreateCommandPool(device, &pool_info, NULL, &command_pool);
    if (res != VK_SUCCESS) {
        LOGE("Failed to create command pool: %s\n", vk_result_str(res));
        return 1;
    }

    /* Allocate command buffer */
    VkCommandBufferAllocateInfo cmd_alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    res = vkAllocateCommandBuffers(device, &cmd_alloc, &cmd);
    if (res != VK_SUCCESS) {
        LOGE("Failed to allocate command buffer: %s\n", vk_result_str(res));
        return 1;
    }

    /* Create color image */
    VkImageCreateInfo img_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = color_format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage color_image;
    res = vkCreateImage(device, &img_info, NULL, &color_image);
    if (res != VK_SUCCESS) {
        LOGE("Failed to create color image: %s\n", vk_result_str(res));
        return 1;
    }

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(device, color_image, &mem_req);
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(chosen, &mem_props);

    uint32_t mem_type_idx = UINT32_MAX;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((mem_req.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            mem_type_idx = i;
            break;
        }
    }
    if (mem_type_idx == UINT32_MAX) {
        LOGE("No suitable memory type for color image\n");
        return 1;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_req.size,
        .memoryTypeIndex = mem_type_idx,
    };
    VkDeviceMemory color_mem;
    res = vkAllocateMemory(device, &alloc_info, NULL, &color_mem);
    if (res != VK_SUCCESS) {
        LOGE("Failed to allocate color image memory\n");
        return 1;
    }
    vkBindImageMemory(device, color_image, color_mem, 0);

    /* Color image view */
    VkImageViewCreateInfo iv_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = color_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = color_format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0, .levelCount = 1,
            .baseArrayLayer = 0, .layerCount = 1,
        },
    };
    VkImageView color_view;
    res = vkCreateImageView(device, &iv_info, NULL, &color_view);
    if (res != VK_SUCCESS) {
        LOGE("Failed to create color image view\n");
        return 1;
    }

    /* Depth image */
    img_info.format = depth_format;
    img_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VkImage depth_image;
    res = vkCreateImage(device, &img_info, NULL, &depth_image);
    if (res != VK_SUCCESS) {
        LOGE("Failed to create depth image: %s\n", vk_result_str(res));
        return 1;
    }
    vkGetImageMemoryRequirements(device, depth_image, &mem_req);
    mem_type_idx = UINT32_MAX;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((mem_req.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            mem_type_idx = i;
            break;
        }
    }
    alloc_info.allocationSize = mem_req.size;
    alloc_info.memoryTypeIndex = mem_type_idx;
    VkDeviceMemory depth_mem;
    res = vkAllocateMemory(device, &alloc_info, NULL, &depth_mem);
    if (res != VK_SUCCESS) {
        LOGE("Failed to allocate depth image memory\n");
        return 1;
    }
    vkBindImageMemory(device, depth_image, depth_mem, 0);

    VkImageViewCreateInfo div_info = iv_info;
    div_info.image = depth_image;
    div_info.format = depth_format;
    div_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    VkImageView depth_view;
    res = vkCreateImageView(device, &div_info, NULL, &depth_view);
    if (res != VK_SUCCESS) {
        LOGE("Failed to create depth image view\n");
        return 1;
    }

    LOGI("Offscreen images created: %dx%d\n", width, height);

    /* -----------------------------------------------------------
     * Stage E: Render pass
     * ----------------------------------------------------------- */
    t0 = clock();
    VkAttachmentDescription color_att = {
        .format = color_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    };

    VkAttachmentDescription depth_att = {
        .format = depth_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
        .pDepthStencilAttachment = &depth_ref,
    };

    VkSubpassDependency dep = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
    };

    VkAttachmentDescription atts[2] = {color_att, depth_att};
    VkRenderPassCreateInfo rp_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments = atts,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dep,
    };
    VkRenderPass render_pass;
    res = vkCreateRenderPass(device, &rp_info, NULL, &render_pass);
    if (res != VK_SUCCESS) {
        LOGE("Failed to create render pass: %s\n", vk_result_str(res));
        return 1;
    }
    LOGI("Render pass created\n");

    /* -----------------------------------------------------------
     * Stage F: Framebuffer
     * ----------------------------------------------------------- */
    VkImageView fbs[2] = {color_view, depth_view};
    VkFramebufferCreateInfo fb_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass,
        .attachmentCount = 2,
        .pAttachments = fbs,
        .width = width,
        .height = height,
        .layers = 1,
    };
    VkFramebuffer framebuffer;
    res = vkCreateFramebuffer(device, &fb_info, NULL, &framebuffer);
    if (res != VK_SUCCESS) {
        LOGE("Failed to create framebuffer: %s\n", vk_result_str(res));
        return 1;
    }
    LOGI("Framebuffer created\n");

    /* -----------------------------------------------------------
     * Stage G: Shader modules from SPIR-V
     * ----------------------------------------------------------- */
    /* Vert shader SPIR-V */
    static const uint32_t kVertSpirv[] = {
        0x07230203u, 0x00010000u, 0x0008000Bu, 0x00000063u, 0x00000000u,
        0x00020011u, 0x00000001u, 0x0006000Bu, 0x00000001u, 0x4C534C47u,
        0x6474732Eu, 0x3035342Eu, 0x00000000u, 0x0003000Eu, 0x00000000u,
        0x00000001u, 0x0007000Fu, 0x00000000u, 0x00000004u, 0x6E69616Du,
        0x00000000u, 0x00000008u, 0x0000005Au, 0x00030003u, 0x00000002u,
        0x000001C2u, 0x00090004u, 0x415F4C47u, 0x735F4252u, 0x72617065u,
        0x5F657461u, 0x64616873u, 0x6F5F7265u, 0x63656A62u, 0x00007374u,
        0x00040005u, 0x00000004u, 0x6E69616Du, 0x00000000u, 0x00060005u,
        0x00000008u, 0x565F6C67u, 0x65747265u, 0x646E4978u, 0x00007865u,
        0x00030005u, 0x00000012u, 0x00000070u, 0x00060005u, 0x00000013u,
        0x68737550u, 0x736E6F43u, 0x746E6174u, 0x00000073u, 0x00040006u,
        0x00000013u, 0x00000000u, 0x00007871u, 0x00040006u, 0x00000013u,
        0x00000001u, 0x00007971u, 0x00030005u, 0x00000015u, 0x00006370u,
        0x00060005u, 0x00000058u, 0x505F6C67u, 0x65567265u, 0x78657472u,
        0x00000000u, 0x00060006u, 0x00000058u, 0x00000000u, 0x505F6C67u,
        0x7469736Fu, 0x006E6F69u, 0x00070006u, 0x00000058u, 0x00000001u,
        0x505F6C67u, 0x746E696Fu, 0x657A6953u, 0x00000000u, 0x00070006u,
        0x00000058u, 0x00000002u, 0x435F6C67u, 0x4470696Cu, 0x61747369u,
        0x0065636Eu, 0x00070006u, 0x00000058u, 0x00000003u, 0x435F6C67u,
        0x446C6C75u, 0x61747369u, 0x0065636Eu, 0x00030005u, 0x0000005Au,
        0x00000000u, 0x00040047u, 0x00000008u, 0x0000000Bu, 0x0000002Au,
        0x00030047u, 0x00000013u, 0x00000002u, 0x00050048u, 0x00000013u,
        0x00000000u, 0x00000023u, 0x00000000u, 0x00050048u, 0x00000013u,
        0x00000001u, 0x00000023u, 0x00000004u, 0x00030047u, 0x00000058u,
        0x00000002u, 0x00050048u, 0x00000058u, 0x00000000u, 0x0000000Bu,
        0x00000000u, 0x00050048u, 0x00000058u, 0x00000001u, 0x0000000Bu,
        0x00000001u, 0x00050048u, 0x00000058u, 0x00000002u, 0x0000000Bu,
        0x00000003u, 0x00050048u, 0x00000058u, 0x00000003u, 0x0000000Bu,
        0x00000004u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u,
        0x00000002u, 0x00040015u, 0x00000006u, 0x00000020u, 0x00000001u,
        0x00040020u, 0x00000007u, 0x00000001u, 0x00000006u, 0x0004003Bu,
        0x00000007u, 0x00000008u, 0x00000001u, 0x0004002Bu, 0x00000006u,
        0x0000000Au, 0x00000000u, 0x00020014u, 0x0000000Bu, 0x00030016u,
        0x0000000Fu, 0x00000020u, 0x00040017u, 0x00000010u, 0x0000000Fu,
        0x00000002u, 0x00040020u, 0x00000011u, 0x00000007u, 0x00000010u,
        0x0004001Eu, 0x00000013u, 0x0000000Fu, 0x0000000Fu, 0x00040020u,
        0x00000014u, 0x00000009u, 0x00000013u, 0x0004003Bu, 0x00000014u,
        0x00000015u, 0x00000009u, 0x00040020u, 0x00000016u, 0x00000009u,
        0x0000000Fu, 0x0004002Bu, 0x00000006u, 0x0000001Au, 0x00000001u,
        0x0004002Bu, 0x00000006u, 0x0000002Au, 0x00000002u, 0x0004002Bu,
        0x00000006u, 0x00000036u, 0x00000003u, 0x0004002Bu, 0x00000006u,
        0x00000042u, 0x00000004u, 0x00040017u, 0x00000054u, 0x0000000Fu,
        0x00000004u, 0x00040015u, 0x00000055u, 0x00000020u, 0x00000000u,
        0x0004002Bu, 0x00000055u, 0x00000056u, 0x00000001u, 0x0004001Cu,
        0x00000057u, 0x0000000Fu, 0x00000056u, 0x0006001Eu, 0x00000058u,
        0x00000054u, 0x0000000Fu, 0x00000057u, 0x00000057u, 0x00040020u,
        0x00000059u, 0x00000003u, 0x00000058u, 0x0004003Bu, 0x00000059u,
        0x0000005Au, 0x00000003u, 0x0004002Bu, 0x0000000Fu, 0x0000005Cu,
        0x00000000u, 0x0004002Bu, 0x0000000Fu, 0x0000005Du, 0x3F800000u,
        0x00040020u, 0x00000061u, 0x00000003u, 0x00000054u, 0x00050036u,
        0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200F8u,
        0x00000005u, 0x0004003Bu, 0x00000011u, 0x00000012u, 0x00000007u,
        0x0004003Du, 0x00000006u, 0x00000009u, 0x00000008u, 0x000500AAu,
        0x0000000Bu, 0x0000000Cu, 0x00000009u, 0x0000000Au, 0x000300F7u,
        0x0000000Eu, 0x00000000u, 0x000400FAu, 0x0000000Cu, 0x0000000Du,
        0x0000001Eu, 0x000200F8u, 0x0000000Du, 0x00050041u, 0x00000016u,
        0x00000017u, 0x00000015u, 0x0000000Au, 0x0004003Du, 0x0000000Fu,
        0x00000018u, 0x00000017u, 0x0004007Fu, 0x0000000Fu, 0x00000019u,
        0x00000018u, 0x00050041u, 0x00000016u, 0x0000001Bu, 0x00000015u,
        0x0000001Au, 0x0004003Du, 0x0000000Fu, 0x0000001Cu, 0x0000001Bu,
        0x00050050u, 0x00000010u, 0x0000001Du, 0x00000019u, 0x0000001Cu,
        0x0003003Eu, 0x00000012u, 0x0000001Du, 0x000200F9u, 0x0000000Eu,
        0x000200F8u, 0x0000001Eu, 0x0004003Du, 0x00000006u, 0x0000001Fu,
        0x00000008u, 0x000500AAu, 0x0000000Bu, 0x00000020u, 0x0000001Fu,
        0x0000001Au, 0x000300F7u, 0x00000022u, 0x00000000u, 0x000400FAu,
        0x00000020u, 0x00000021u, 0x00000028u, 0x000200F8u, 0x00000021u,
        0x00050041u, 0x00000016u, 0x00000023u, 0x00000015u, 0x0000000Au,
        0x0004003Du, 0x0000000Fu, 0x00000024u, 0x00000023u, 0x00050041u,
        0x00000016u, 0x00000025u, 0x00000015u, 0x0000001Au, 0x0004003Du,
        0x0000000Fu, 0x00000026u, 0x00000025u, 0x00050050u, 0x00000010u,
        0x00000027u, 0x00000024u, 0x00000026u, 0x0003003Eu, 0x00000012u,
        0x00000027u, 0x000200F9u, 0x00000022u, 0x000200F8u, 0x00000028u,
        0x0004003Du, 0x00000006u, 0x00000029u, 0x00000008u, 0x000500AAu,
        0x0000000Bu, 0x0000002Bu, 0x00000029u, 0x0000002Au, 0x000300F7u,
        0x0000002Du, 0x00000000u, 0x000400FAu, 0x0000002Bu, 0x0000002Cu,
        0x00000034u, 0x000200F8u, 0x0000002Cu, 0x00050041u, 0x00000016u,
        0x0000002Eu, 0x00000015u, 0x0000000Au, 0x0004003Du, 0x0000000Fu,
        0x0000002Fu, 0x0000002Eu, 0x00050041u, 0x00000016u, 0x00000030u,
        0x00000015u, 0x0000001Au, 0x0004003Du, 0x0000000Fu, 0x00000031u,
        0x00000030u, 0x0004007Fu, 0x0000000Fu, 0x00000032u, 0x00000031u,
        0x00050050u, 0x00000010u, 0x00000033u, 0x0000002Fu, 0x00000032u,
        0x0003003Eu, 0x00000012u, 0x00000033u, 0x000200F9u, 0x0000002Du,
        0x000200F8u, 0x00000034u, 0x0004003Du, 0x00000006u, 0x00000035u,
        0x00000008u, 0x000500AAu, 0x0000000Bu, 0x00000037u, 0x00000035u,
        0x00000036u, 0x000300F7u, 0x00000039u, 0x00000000u, 0x000400FAu,
        0x00000037u, 0x00000038u, 0x00000040u, 0x000200F8u, 0x00000038u,
        0x00050041u, 0x00000016u, 0x0000003Au, 0x00000015u, 0x0000000Au,
        0x0004003Du, 0x0000000Fu, 0x0000003Bu, 0x0000003Au, 0x0004007Fu,
        0x0000000Fu, 0x0000003Cu, 0x0000003Bu, 0x00050041u, 0x00000016u,
        0x0000003Du, 0x00000015u, 0x0000001Au, 0x0004003Du, 0x0000000Fu,
        0x0000003Eu, 0x0000003Du, 0x00050050u, 0x00000010u, 0x0000003Fu,
        0x0000003Cu, 0x0000003Eu, 0x0003003Eu, 0x00000012u, 0x0000003Fu,
        0x000200F9u, 0x00000039u, 0x000200F8u, 0x00000040u, 0x0004003Du,
        0x00000006u, 0x00000041u, 0x00000008u, 0x000500AAu, 0x0000000Bu,
        0x00000043u, 0x00000041u, 0x00000042u, 0x000300F7u, 0x00000045u,
        0x00000000u, 0x000400FAu, 0x00000043u, 0x00000044u, 0x0000004Cu,
        0x000200F8u, 0x00000044u, 0x00050041u, 0x00000016u, 0x00000046u,
        0x00000015u, 0x0000000Au, 0x0004003Du, 0x0000000Fu, 0x00000047u,
        0x00000046u, 0x00050041u, 0x00000016u, 0x00000048u, 0x00000015u,
        0x0000001Au, 0x0004003Du, 0x0000000Fu, 0x00000049u, 0x00000048u,
        0x0004007Fu, 0x0000000Fu, 0x0000004Au, 0x00000049u, 0x00050050u,
        0x00000010u, 0x0000004Bu, 0x00000047u, 0x0000004Au, 0x0003003Eu,
        0x00000012u, 0x0000004Bu, 0x000200F9u, 0x00000045u, 0x000200F8u,
        0x0000004Cu, 0x00050041u, 0x00000016u, 0x0000004Du, 0x00000015u,
        0x0000000Au, 0x0004003Du, 0x0000000Fu, 0x0000004Eu, 0x0000004Du,
        0x0004007Fu, 0x0000000Fu, 0x0000004Fu, 0x0000004Eu, 0x00050041u,
        0x00000016u, 0x00000050u, 0x00000015u, 0x0000001Au, 0x0004003Du,
        0x0000000Fu, 0x00000051u, 0x00000050u, 0x0004007Fu, 0x0000000Fu,
        0x00000052u, 0x00000051u, 0x00050050u, 0x00000010u, 0x00000053u,
        0x0000004Fu, 0x00000052u, 0x0003003Eu, 0x00000012u, 0x00000053u,
        0x000200F9u, 0x00000045u, 0x000200F8u, 0x00000045u, 0x000200F9u,
        0x00000039u, 0x000200F8u, 0x00000039u, 0x000200F9u, 0x0000002Du,
        0x000200F8u, 0x0000002Du, 0x000200F9u, 0x00000022u, 0x000200F8u,
        0x00000022u, 0x000200F9u, 0x0000000Eu, 0x000200F8u, 0x0000000Eu,
        0x0004003Du, 0x00000010u, 0x0000005Bu, 0x00000012u, 0x00050051u,
        0x0000000Fu, 0x0000005Eu, 0x0000005Bu, 0x00000000u, 0x00050051u,
        0x0000000Fu, 0x0000005Fu, 0x0000005Bu, 0x00000001u, 0x00070050u,
        0x00000054u, 0x00000060u, 0x0000005Eu, 0x0000005Fu, 0x0000005Cu,
        0x0000005Du, 0x00050041u, 0x00000061u, 0x00000062u, 0x0000005Au,
        0x0000000Au, 0x0003003Eu, 0x00000062u, 0x00000060u, 0x000100FDu,
        0x00010038u
    };

    /* Frag shader SPIR-V */
    static const uint32_t kFragSpirv[] = {
        0x07230203u, 0x00010000u, 0x0008000Bu, 0x0000006Cu, 0x00000000u,
        0x00020011u, 0x00000001u, 0x0006000Bu, 0x00000001u, 0x4C534C47u,
        0x6474732Eu, 0x3035342Eu, 0x00000000u, 0x0003000Eu, 0x00000000u,
        0x00000001u, 0x0007000Fu, 0x00000004u, 0x00000004u, 0x6E69616Du,
        0x00000000u, 0x0000003Bu, 0x00000066u, 0x00030010u, 0x00000004u,
        0x00000007u, 0x00030003u, 0x00000002u, 0x000001C2u, 0x00090004u,
        0x415F4C47u, 0x735F4252u, 0x72617065u, 0x5F657461u, 0x64616873u,
        0x6F5F7265u, 0x63656A62u, 0x00007374u, 0x00040005u, 0x00000004u,
        0x6E69616Du, 0x00000000u, 0x00070005u, 0x0000000Bu, 0x65706170u,
        0x72675F72u, 0x286E6961u, 0x3B326676u, 0x00000000u, 0x00030005u,
        0x0000000Au, 0x00007675u, 0x00070005u, 0x0000000Eu, 0x65706170u,
        0x696C5F72u, 0x69746867u, 0x7628676Eu, 0x003B3266u, 0x00030005u,
        0x0000000Du, 0x00007675u, 0x00030005u, 0x00000011u, 0x0000006Eu,
        0x00050005u, 0x00000020u, 0x6867696Cu, 0x69645F74u, 0x00000072u,
        0x00040005u, 0x00000024u, 0x6D726F6Eu, 0x00006C61u, 0x00040005u,
        0x00000028u, 0x66666964u, 0x00000000u, 0x00040005u, 0x00000034u,
        0x65736162u, 0x00000000u, 0x00030005u, 0x00000038u, 0x00007675u,
        0x00060005u, 0x0000003Bu, 0x465F6C67u, 0x43676172u, 0x64726F6Fu,
        0x00000000u, 0x00040005u, 0x00000042u, 0x69617267u, 0x0000006Eu,
        0x00040005u, 0x00000046u, 0x61726170u, 0x0000006Du, 0x00060005u,
        0x0000004Au, 0x65676465u, 0x6769765Fu, 0x7474656Eu, 0x00000065u,
        0x00030005u, 0x00000058u, 0x0074696Cu, 0x00040005u, 0x0000005Au,
        0x61726170u, 0x0000006Du, 0x00040005u, 0x00000066u, 0x6C6F436Fu,
        0x0000726Fu, 0x00040047u, 0x0000003Bu, 0x0000000Bu, 0x0000000Fu,
        0x00040047u, 0x00000066u, 0x0000001Eu, 0x00000000u, 0x00020013u,
        0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00030016u,
        0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u,
        0x00000002u, 0x00040020u, 0x00000008u, 0x00000007u, 0x00000007u,
        0x00040021u, 0x00000009u, 0x00000006u, 0x00000008u, 0x00040020u,
        0x00000010u, 0x00000007u, 0x00000006u, 0x0004002Bu, 0x00000006u,
        0x00000013u, 0x414FD639u, 0x0004002Bu, 0x00000006u, 0x00000014u,
        0x429C774Cu, 0x0005002Cu, 0x00000007u, 0x00000015u, 0x00000013u,
        0x00000014u, 0x0004002Bu, 0x00000006u, 0x00000018u, 0x472AEE8Cu,
        0x00040017u, 0x0000001Eu, 0x00000006u, 0x00000003u, 0x00040020u,
        0x0000001Fu, 0x00000007u, 0x0000001Eu, 0x0004002Bu, 0x00000006u,
        0x00000021u, 0x3ED105ECu, 0x0004002Bu, 0x00000006u, 0x00000022u,
        0xBF5105ECu, 0x0006002Cu, 0x0000001Eu, 0x00000023u, 0x00000021u,
        0x00000022u, 0x00000021u, 0x0004002Bu, 0x00000006u, 0x00000025u,
        0x00000000u, 0x0004002Bu, 0x00000006u, 0x00000026u, 0x3F800000u,
        0x0006002Cu, 0x0000001Eu, 0x00000027u, 0x00000025u, 0x00000025u,
        0x00000026u, 0x0004002Bu, 0x00000006u, 0x0000002Du, 0x3F400000u,
        0x0004002Bu, 0x00000006u, 0x0000002Eu, 0x3E800000u, 0x0004002Bu,
        0x00000006u, 0x00000035u, 0x3F70A3D7u, 0x0004002Bu, 0x00000006u,
        0x00000036u, 0x3F666666u, 0x0006002Cu, 0x0000001Eu, 0x00000037u,
        0x00000035u, 0x00000035u, 0x00000036u, 0x00040017u, 0x00000039u,
        0x00000006u, 0x00000004u, 0x00040020u, 0x0000003Au, 0x00000001u,
        0x00000039u, 0x0004003Bu, 0x0000003Au, 0x0000003Bu, 0x00000001u,
        0x0004002Bu, 0x00000006u, 0x0000003Eu, 0x44F00000u, 0x0004002Bu,
        0x00000006u, 0x0000003Fu, 0x44870000u, 0x0005002Cu, 0x00000007u,
        0x00000040u, 0x0000003Eu, 0x0000003Fu, 0x0004002Bu, 0x00000006u,
        0x00000044u, 0x41000000u, 0x0004002Bu, 0x00000006u, 0x00000048u,
        0x3CA3D70Au, 0x0004002Bu, 0x00000006u, 0x0000004Bu, 0x3D4CCCCDu,
        0x00040015u, 0x0000004Cu, 0x00000020u, 0x00000000u, 0x0004002Bu,
        0x0000004Cu, 0x0000004Du, 0x00000000u, 0x0004002Bu, 0x0000004Cu,
        0x00000052u, 0x00000001u, 0x00040020u, 0x00000065u, 0x00000003u,
        0x00000039u, 0x0004003Bu, 0x00000065u, 0x00000066u, 0x00000003u,
        0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u,
        0x000200F8u, 0x00000005u, 0x0004003Bu, 0x0000001Fu, 0x00000034u,
        0x00000007u, 0x0004003Bu, 0x00000008u, 0x00000038u, 0x00000007u,
        0x0004003Bu, 0x00000010u, 0x00000042u, 0x00000007u, 0x0004003Bu,
        0x00000008u, 0x00000046u, 0x00000007u, 0x0004003Bu, 0x00000010u,
        0x0000004Au, 0x00000007u, 0x0004003Bu, 0x0000001Fu, 0x00000058u,
        0x00000007u, 0x0004003Bu, 0x00000008u, 0x0000005Au, 0x00000007u,
        0x0003003Eu, 0x00000034u, 0x00000037u, 0x0004003Du, 0x00000039u,
        0x0000003Cu, 0x0000003Bu, 0x0007004Fu, 0x00000007u, 0x0000003Du,
        0x0000003Cu, 0x0000003Cu, 0x00000000u, 0x00000001u, 0x00050088u,
        0x00000007u, 0x00000041u, 0x0000003Du, 0x00000040u, 0x0003003Eu,
        0x00000038u, 0x00000041u, 0x0004003Du, 0x00000007u, 0x00000043u,
        0x00000038u, 0x0005008Eu, 0x00000007u, 0x00000045u, 0x00000043u,
        0x00000044u, 0x0003003Eu, 0x00000046u, 0x00000045u, 0x00050039u,
        0x00000006u, 0x00000047u, 0x0000000Bu, 0x00000046u, 0x00050085u,
        0x00000006u, 0x00000049u, 0x00000047u, 0x00000048u, 0x0003003Eu,
        0x00000042u, 0x00000049u, 0x00050041u, 0x00000010u, 0x0000004Eu,
        0x00000038u, 0x0000004Du, 0x0004003Du, 0x00000006u, 0x0000004Fu,
        0x0000004Eu, 0x00050083u, 0x00000006u, 0x00000050u, 0x00000026u,
        0x0000004Fu, 0x00050085u, 0x00000006u, 0x00000051u, 0x0000004Bu,
        0x00000050u, 0x00050041u, 0x00000010u, 0x00000053u, 0x00000038u,
        0x00000052u, 0x0004003Du, 0x00000006u, 0x00000054u, 0x00000053u,
        0x00050083u, 0x00000006u, 0x00000055u, 0x00000026u, 0x00000054u,
        0x00050085u, 0x00000006u, 0x00000056u, 0x00000051u, 0x00000055u,
        0x00050083u, 0x00000006u, 0x00000057u, 0x00000026u, 0x00000056u,
        0x0003003Eu, 0x0000004Au, 0x00000057u, 0x0004003Du, 0x0000001Eu,
        0x00000059u, 0x00000034u, 0x0004003Du, 0x00000007u, 0x0000005Bu,
        0x00000038u, 0x0003003Eu, 0x0000005Au, 0x0000005Bu, 0x00050039u,
        0x00000006u, 0x0000005Cu, 0x0000000Eu, 0x0000005Au, 0x0005008Eu,
        0x0000001Eu, 0x0000005Du, 0x00000059u, 0x0000005Cu, 0x0003003Eu,
        0x00000058u, 0x0000005Du, 0x0004003Du, 0x00000006u, 0x0000005Eu,
        0x00000042u, 0x0004003Du, 0x0000001Eu, 0x0000005Fu, 0x00000058u,
        0x00060050u, 0x0000001Eu, 0x00000060u, 0x0000005Eu, 0x0000005Eu,
        0x0000005Eu, 0x00050081u, 0x0000001Eu, 0x00000061u, 0x0000005Fu,
        0x00000060u, 0x0003003Eu, 0x00000058u, 0x00000061u, 0x0004003Du,
        0x00000006u, 0x00000062u, 0x0000004Au, 0x0004003Du, 0x0000001Eu,
        0x00000063u, 0x00000058u, 0x0005008Eu, 0x0000001Eu, 0x00000064u,
        0x00000063u, 0x00000062u, 0x0003003Eu, 0x00000058u, 0x00000064u,
        0x0004003Du, 0x0000001Eu, 0x00000067u, 0x00000058u, 0x00050051u,
        0x00000006u, 0x00000068u, 0x00000067u, 0x00000000u, 0x00050051u,
        0x00000006u, 0x00000069u, 0x00000067u, 0x00000001u, 0x00050051u,
        0x00000006u, 0x0000006Au, 0x00000067u, 0x00000002u, 0x00070050u,
        0x00000039u, 0x0000006Bu, 0x00000068u, 0x00000069u, 0x0000006Au,
        0x00000026u, 0x0003003Eu, 0x00000066u, 0x0000006Bu, 0x000100FDu,
        0x00010038u, 0x00050036u, 0x00000006u, 0x0000000Bu, 0x00000000u,
        0x00000009u, 0x00030037u, 0x00000008u, 0x0000000Au, 0x000200F8u,
        0x0000000Cu, 0x0004003Bu, 0x00000010u, 0x00000011u, 0x00000007u,
        0x0004003Du, 0x00000007u, 0x00000012u, 0x0000000Au, 0x00050094u,
        0x00000006u, 0x00000016u, 0x00000012u, 0x00000015u, 0x0006000Cu,
        0x00000006u, 0x00000017u, 0x00000001u, 0x0000000Du, 0x00000016u,
        0x00050085u, 0x00000006u, 0x00000019u, 0x00000017u, 0x00000018u,
        0x0006000Cu, 0x00000006u, 0x0000001Au, 0x00000001u, 0x0000000Au,
        0x00000019u, 0x0003003Eu, 0x00000011u, 0x0000001Au, 0x0004003Du,
        0x00000006u, 0x0000001Bu, 0x00000011u, 0x000200FEu, 0x0000001Bu,
        0x00010038u, 0x00050036u, 0x00000006u, 0x0000000Eu, 0x00000000u,
        0x00000009u, 0x00030037u, 0x00000008u, 0x0000000Du, 0x000200F8u,
        0x0000000Fu, 0x0004003Bu, 0x0000001Fu, 0x00000020u, 0x00000007u,
        0x0004003Bu, 0x0000001Fu, 0x00000024u, 0x00000007u, 0x0004003Bu,
        0x00000010u, 0x00000028u, 0x00000007u, 0x0003003Eu, 0x00000020u,
        0x00000023u, 0x0003003Eu, 0x00000024u, 0x00000027u, 0x0004003Du,
        0x0000001Eu, 0x00000029u, 0x00000024u, 0x0004003Du, 0x0000001Eu,
        0x0000002Au, 0x00000020u, 0x00050094u, 0x00000006u, 0x0000002Bu,
        0x00000029u, 0x0000002Au, 0x0007000Cu, 0x00000006u, 0x0000002Cu,
        0x00000001u, 0x00000028u, 0x0000002Bu, 0x00000025u, 0x0003003Eu,
        0x00000028u, 0x0000002Cu, 0x0004003Du, 0x00000006u, 0x0000002Fu,
        0x00000028u, 0x00050085u, 0x00000006u, 0x00000030u, 0x0000002Eu,
        0x0000002Fu, 0x00050081u, 0x00000006u, 0x00000031u, 0x0000002Du,
        0x00000030u, 0x000200FEu, 0x00000031u, 0x00010038u
    };

    t0 = clock();
    VkShaderModule vert_module, frag_module;
    res = vkCreateShaderModule(device, &(VkShaderModuleCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(kVertSpirv),
        .pCode = kVertSpirv,
    }, NULL, &vert_module);
    if (res != VK_SUCCESS) {
        LOGE("Failed to create vertex shader module: %s\n", vk_result_str(res));
        return 1;
    }
    res = vkCreateShaderModule(device, &(VkShaderModuleCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(kFragSpirv),
        .pCode = kFragSpirv,
    }, NULL, &frag_module);
    if (res != VK_SUCCESS) {
        LOGE("Failed to create fragment shader module: %s\n", vk_result_str(res));
        return 1;
    }
    LOGI("Shader modules created\n");

    /* -----------------------------------------------------------
     * Stage H: Graphics pipeline
     * ----------------------------------------------------------- */
    VkPushConstantRange pc_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(float) * 2,
    };

    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
        .pSetLayouts = NULL,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pc_range,
    };
    VkPipelineLayout pipeline_layout;
    res = vkCreatePipelineLayout(device, &layout_info, NULL, &pipeline_layout);
    if (res != VK_SUCCESS) {
        LOGE("Failed to create pipeline layout: %s\n", vk_result_str(res));
        return 1;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main" },
    };

    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .vertexAttributeDescriptionCount = 0,
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
    };
    VkPipelineColorBlendAttachmentState cb_att = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = VK_FALSE,
    };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &cb_att,
    };
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn,
    };

    VkGraphicsPipelineCreateInfo gp_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = &ds,
        .pColorBlendState = &cb,
        .pDynamicState = &dyn_state,
        .layout = pipeline_layout,
        .renderPass = render_pass,
        .subpass = 0,
    };

    VkPipeline pipeline;
    t0 = clock();
    res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp_info, NULL, &pipeline);
    t1 = clock();
    if (res != VK_SUCCESS) {
        LOGE("Failed to create graphics pipeline: %s\n", vk_result_str(res));
        return 1;
    }
    LOGI("Graphics pipeline created (%.2f ms)\n",
         (double)(t1 - t0) * 1000.0 / CLOCKS_PER_SEC);

    vkDestroyShaderModule(device, vert_module, NULL);
    vkDestroyShaderModule(device, frag_module, NULL);

    /* -----------------------------------------------------------
     * Stage I: Record and submit command buffer
     * ----------------------------------------------------------- */
    VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    vkCreateFence(device, &fence_info, NULL, &fence);

    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin);

    /* Transition color image to color attachment */
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image = color_image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);

    /* Transition depth image */
    VkImageMemoryBarrier depth_barrier = barrier;
    depth_barrier.image = depth_image;
    depth_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depth_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_barrier.srcAccessMask = 0;
    depth_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0, 0, NULL, 0, NULL, 1, &depth_barrier);

    /* Begin render pass */
    VkClearValue clear_vals[2] = {
        { {0.94f, 0.94f, 0.90f, 1.0f} },  /* off-white paper color */
        { {1.0f, 0} },                    /* depth clear */
    };
    VkRenderPassBeginInfo rp_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {width, height}},
        .clearValueCount = 2,
        .pClearValues = clear_vals,
    };
    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    /* Push constants: quad covering 85% of screen */
    float pc[2] = {0.85f, 0.85f};
    vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(pc), pc);

    VkViewport viewport = {0, 0, (float)width, (float)height, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {width, height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(cmd, 6, 1, 0, 0);

    vkCmdEndRenderPass(cmd);

    /* Transition color image to transfer source for readback */
    VkImageMemoryBarrier readback_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .image = color_image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &readback_barrier);

    vkEndCommandBuffer(cmd);

    /* Submit */
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    t0 = clock();
    res = vkQueueSubmit(queue, 1, &submit, fence);
    if (res != VK_SUCCESS) {
        LOGE("Queue submit failed: %s\n", vk_result_str(res));
        return 1;
    }

    /* Wait for fence */
    res = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (res != VK_SUCCESS) {
        LOGE("Wait for fences failed: %s\n", vk_result_str(res));
        return 1;
    }
    t1 = clock();
    LOGI("Frame submitted and completed (%.2f ms)\n",
         (double)(t1 - t0) * 1000.0 / CLOCKS_PER_SEC);

    /* -----------------------------------------------------------
     * Stage J: Read back pixels and verify rendering
     * ----------------------------------------------------------- */
    /* Create staging buffer for readback */
    VkDeviceSize row_pitch = ((width * 4 + 3) / 4) * 4;  /* align to 4 bytes */
    VkDeviceSize buffer_size = row_pitch * height;

    VkBufferCreateInfo buf_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer readback_buf;
    res = vkCreateBuffer(device, &buf_info, NULL, &readback_buf);
    if (res != VK_SUCCESS) {
        LOGE("Failed to create readback buffer\n");
        return 1;
    }
    vkGetBufferMemoryRequirements(device, readback_buf, &mem_req);
    mem_type_idx = UINT32_MAX;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((mem_req.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
            mem_type_idx = i;
            break;
        }
    }
    if (mem_type_idx == UINT32_MAX) {
        LOGE("No host-visible coherent memory type for readback\n");
        return 1;
    }
    alloc_info.allocationSize = mem_req.size;
    alloc_info.memoryTypeIndex = mem_type_idx;
    VkDeviceMemory readback_mem;
    res = vkAllocateMemory(device, &alloc_info, NULL, &readback_mem);
    if (res != VK_SUCCESS) {
        LOGE("Failed to allocate readback memory\n");
        return 1;
    }
    vkBindBufferMemory(device, readback_buf, readback_mem, 0);

    /* Copy image to buffer */
    vkResetCommandBuffer(cmd, 0);
    vkBeginCommandBuffer(cmd, &begin);

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = width,
        .bufferImageHeight = 0,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1},
    };
    vkCmdCopyImageToBuffer(cmd,
        color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        readback_buf, 1, &region);

    vkEndCommandBuffer(cmd);
    vkResetFences(device, 1, &fence);
    vkQueueSubmit(queue, 1, &submit, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    /* Map and inspect pixels */
    void *mapped;
    vkMapMemory(device, readback_mem, 0, buffer_size, 0, &mapped);

    /* Sample pixels at key locations */
    uint8_t *pixels = (uint8_t *)mapped;
    int samples[] = {0, width/4, width/2, 3*width/4, width-1};
    LOGI("\n=== Pixel sampling (RGBA) ===\n");
    for (int sy = 0; sy < 5; sy++) {
        int y = samples[sy];
        for (int sx = 0; sx < 5; sx++) {
            int x = samples[sx];
            size_t offset = (size_t)y * row_pitch + (size_t)x * 4;
            uint8_t r = pixels[offset];
            uint8_t g = pixels[offset + 1];
            uint8_t b = pixels[offset + 2];
            uint8_t a = pixels[offset + 3];
            LOGI("  (%4d,%4d): R=%3u G=%3u B=%3u A=%3u\n", x, y, r, g, b, a);
        }
    }

    /* Check if we rendered something non-trivial (not all clear color) */
    int non_clear_pixels = 0;
    for (size_t i = 0; i < buffer_size; i += 4) {
        if (pixels[i] != 240 || pixels[i+1] != 240 || pixels[i+2] != 229 ||
            pixels[i+3] != 255) {
            non_clear_pixels++;
        }
    }
    LOGI("\nNon-clear pixels: %d / %zu (%.1f%%)\n",
         non_clear_pixels, buffer_size / 4,
         100.0 * non_clear_pixels / (buffer_size / 4));

    if (non_clear_pixels == 0) {
        LOGE("RENDERING FAILED: All pixels are clear color - nothing was drawn\n");
        failures++;
    } else {
        LOGI("RENDERING SUCCESS: Non-clear pixels detected - geometry rendered\n");
    }

    /* Cleanup */
    vkUnmapMemory(device, readback_mem);
    vkFreeMemory(device, readback_mem, NULL);
    vkDestroyBuffer(device, readback_buf, NULL);
    vkDestroyFence(device, fence, NULL);
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyImageView(device, depth_view, NULL);
    vkDestroyImageView(device, color_view, NULL);
    vkDestroyImage(device, depth_image, NULL);
    vkFreeMemory(device, depth_mem, NULL);
    vkDestroyImage(device, color_image, NULL);
    vkFreeMemory(device, color_mem, NULL);
    vkDestroyCommandPool(device, command_pool, NULL);
    vkDestroyDevice(device, NULL);
#ifdef VK_EXT_DEBUG_REPORT_EXTENSION_NAME
    if (vkCreateDebugReportCallbackEXT) {
        PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT =
            (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
        if (vkDestroyDebugReportCallbackEXT)
            vkDestroyDebugReportCallbackEXT(instance, debug_callback_handle, NULL);
    }
#endif
    vkDestroyInstance(instance, NULL);
    free(gpus);
    free(qfps);

    LOGI("\n=== Test complete: %d failure(s) ===\n", failures);
    return failures > 0 ? 1 : 0;
}
