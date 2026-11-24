/*
 * noat_spirv_test.c - Validate SPIR-V shader modules from noat_pipeline.c
 *
 * Extracts the embedded SPIR-V arrays from noat_pipeline.c and verifies
 * that vkCreateShaderModule accepts them on the Mali-G610 MC6.
 *
 * Build:
 *   clang -std=c11 -Wall -Wextra -O2 -I app/src/main/cpp/vulkan \
 *       noat_spirv_test.c -L/system/lib64 -lvulkan -o noat_spirv_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

static const char *vk_result_str(VkResult r) {
    static const char *s[] = {
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
    int idx = (int)(-r);
    if (idx >= 0 && idx < 22) return s[idx];
    return "UNKNOWN";
}

/* Include the SPIR-V arrays extracted from noat_pipeline.c */
#include "spirv_arrays.h"

int main(void) {
    VkInstance instance;
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "noat-spirv-test",
        .apiVersion = VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
    };
    VkResult res = vkCreateInstance(&inst_info, NULL, &instance);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "Failed to create instance: %s\n", vk_result_str(res));
        return 1;
    }

    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(instance, &gpu_count, NULL);
    VkPhysicalDevice *gpus = malloc(gpu_count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(instance, &gpu_count, gpus);
    VkPhysicalDevice chosen = gpus[0];
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(chosen, &props);
    printf("Device: %s (Vulkan %d.%d.%d)\n",
           props.deviceName,
           VK_VERSION_MAJOR(props.apiVersion),
           VK_VERSION_MINOR(props.apiVersion),
           VK_VERSION_PATCH(props.apiVersion));

    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(chosen, &qf_count, NULL);
    VkQueueFamilyProperties *qfps = malloc(qf_count * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(chosen, &qf_count, qfps);
    uint32_t graphics_qf = 0;
    for (uint32_t i = 0; i < qf_count; i++) {
        if (qfps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { graphics_qf = i; break; }
    }

    float qp = 1.0f;
    VkDeviceQueueCreateInfo qinfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphics_qf,
        .queueCount = 1,
        .pQueuePriorities = &qp,
    };
    VkDevice device;
    VkDeviceCreateInfo dinfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qinfo,
    };
    res = vkCreateDevice(chosen, &dinfo, NULL, &device);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "Failed to create device: %s\n", vk_result_str(res));
        return 1;
    }

    /* Test vertex shader module */
    VkShaderModule vert_module;
    res = vkCreateShaderModule(device, &(VkShaderModuleCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(kVertSpirv),
        .pCode = kVertSpirv,
    }, NULL, &vert_module);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "Vertex shader module FAILED: %s\n", vk_result_str(res));
        return 1;
    }
    printf("Vertex shader module: OK (%zu bytes, %zu words)\n",
           sizeof(kVertSpirv), sizeof(kVertSpirv) / 4);

    /* Test fragment shader module */
    VkShaderModule frag_module;
    res = vkCreateShaderModule(device, &(VkShaderModuleCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(kFragSpirv),
        .pCode = kFragSpirv,
    }, NULL, &frag_module);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "Fragment shader module FAILED: %s\n", vk_result_str(res));
        return 1;
    }
    printf("Fragment shader module: OK (%zu bytes, %zu words)\n",
           sizeof(kFragSpirv), sizeof(kFragSpirv) / 4);

    vkDestroyShaderModule(device, vert_module, NULL);
    vkDestroyShaderModule(device, frag_module, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    free(gpus);
    free(qfps);

    printf("\nSPIR-V validation: PASSED\n");
    return 0;
}
