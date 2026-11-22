/*
 * noat_resources.c - Vulkan resources (descriptor sets, uniform buffers, samplers)
 */

#include "noat_vulkan.h"
#include <android/log.h>
#include <string.h>

#define TAG "NoatResources"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

struct NoatResources {
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkCommandPool command_pool;
    VkQueue queue;
    uint32_t swapchain_image_count;

    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet *descriptor_sets;

    VkBuffer *uniform_buffers;
    VkDeviceMemory *uniform_buffer_memories;
    VkDeviceSize uniform_buffer_size;
    uint32_t swapchain_image_count;
};

NoatResources *noat_resources_create(VkDevice device, VkPhysicalDevice physical_device,
                                      VkCommandPool command_pool, VkQueue queue,
                                      uint32_t swapchain_image_count) {
    NoatResources *res = calloc(1, sizeof(NoatResources));
    if (!res) return NULL;

    res->device = device;
    res->physical_device = physical_device;
    res->command_pool = command_pool;
    res->queue = queue;
    res->swapchain_image_count = swapchain_image_count;

    if (!noat_resources_create_descriptor_set_layout(res)) return NULL;
    if (!noat_resources_create_descriptor_pool(res, swapchain_image_count)) return NULL;
    if (!noat_resources_create_descriptor_sets(res, swapchain_image_count)) return NULL;

    VkDeviceSize buffer_size = 256;
    if (!noat_resources_create_uniform_buffers(res, 256)) return NULL;

    LOGI("Vulkan resources initialized");
    return res;
}

void noat_resources_destroy(NoatResources *res) {
    if (!res) return;

    for (uint32_t i = 0; i < res->swapchain_image_count; i++) {
        if (res->uniform_buffers[i]) vkDestroyBuffer(res->device, res->uniform_buffers[i], NULL);
        if (res->uniform_buffer_memories[i]) vkFreeMemory(res->device, res->uniform_buffer_memories[i], NULL);
    }
    free(res->uniform_buffers);
    free(res->uniform_buffer_memories);

    if (res->descriptor_pool) vkDestroyDescriptorPool(res->device, res->descriptor_pool, NULL);
    if (res->descriptor_set_layout) vkDestroyDescriptorSetLayout(res->device, res->descriptor_set_layout, NULL);
    if (res->descriptor_sets) free(res->descriptor_sets);

    free(res);
}

int noat_resources_create_descriptor_set_layout(NoatResources *res) {
    VkDescriptorSetLayoutBinding ubo_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayoutBinding sampler_binding = {
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayoutBinding bindings[2] = {ubo_binding, sampler_binding};
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = (VkDescriptorSetLayoutBinding[]){ubb, sampler_binding},
    };

    if (vkCreateDescriptorSetLayout(device, &layout_info, NULL, &res->descriptor_set_layout) != VK_SUCCESS) {
        LOGE("Failed to create descriptor set layout");
        return 0;
    }
    return 1;
}

int noat_resources_create_descriptor_pool(NoatResources *res, uint32_t max_sets) {
    VkDescriptorPoolSize pool_sizes[2] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, max_sets},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_sets},
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = 2,
        .pPoolSizes = (VkDescriptorPoolSize[2]){{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, max_sets}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_sets}},
        .maxSets = max_sets,
    };

    if (vkCreateDescriptorPool(res->device, &pool_info, NULL, &res->descriptor_pool) != VK_SUCCESS) {
        LOGE("Failed to create descriptor pool");
        return 0;
    }
    return 1;
}

int noat_resources_create_descriptor_sets(NoatResources *res, uint32_t count) {
    VkDescriptorSetLayout layouts[count];
    for (uint32_t i = 0; i < count; i++) layouts[i] = res->descriptor_set_layout;

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = res->descriptor_pool,
        .descriptorSetCount = count,
        .pSetLayouts = layouts,
    };

    res->descriptor_sets = malloc(count * sizeof(VkDescriptorSet));
    if (vkAllocateDescriptorSets(res->device, &(VkDescriptorSetAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = res->descriptor_pool,
        .descriptorSetCount = count,
        .pSetLayouts = layouts,
    }, res->descriptor_sets) != VK_SUCCESS) {
        LOGE("Failed to allocate descriptor sets");
        return 0;
    }
    return 1;
}

int noat_resources_create_uniform_buffers(NoatResources *res, VkDeviceSize buffer_size) {
    res->uniform_buffer_size = buffer_size;
    res->uniform_buffers = malloc(res->swapchain_image_count * sizeof(VkBuffer));
    res->uniform_buffer_memories = malloc(res->swapchain_image_count * sizeof(VkDeviceMemory));

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(res->physical_device, &mem_props);

    for (uint32_t i = 0; i < res->swapchain_image_count; i++) {
        if (vkCreateBuffer(res->device, &(VkBufferCreateInfo){
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buffer_size,
            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        }, NULL, &res->uniform_buffers[i]) != VK_SUCCESS) {
            LOGE("Failed to create uniform buffer %zu", i);
            return 0;
        }

        VkMemoryRequirements mem_req;
        vkGetBufferMemoryRequirements(res->device, res->uniform_buffers[i], &mem_req);

        uint32_t mem_type = UINT32_MAX;
        for (uint32_t j = 0; j < 32; j++) {
            if ((mem_req.memoryTypeBits & (1u << j)) &&
                (mem_properties.memoryTypes[j].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
                mem_type = j;
                break;
            }
        }

        if (vkAllocateMemory(res->device, &(VkMemoryAllocateInfo){
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = mem_req.size,
            .memoryTypeIndex = mem_type,
        }, NULL, &res->uniform_buffer_memories[i]) != VK_SUCCESS) {
            LOGE("Failed to allocate uniform buffer memory %zu", i);
            return 0;
        }

        vkBindBufferMemory(res->device, res->uniform_buffers[i], res->uniform_buffer_memories[i], 0);
    }
    return 1;
}

void noat_resources_update_uniform_buffer(NoatResources *res, uint32_t frame_index, const void *data, size_t size) {
    void *mapped;
    vkMapMemory(res->device, res->uniform_buffer_memories[frame_index], 0, size, 0, &mapped);
    memcpy(mapped, data, size);
    vkUnmapMemory(res->device, res->uniform_buffer_memories[frame_index]);
}

VkDescriptorSetLayout noat_resources_get_descriptor_set_layout(const NoatResources *res) {
    return res->descriptor_set_layout;
}

const VkDescriptorSet *noat_resources_get_descriptor_sets(const NoatResources *res, uint32_t *count) {
    if (count) *count = res->swapchain_image_count;
    return res->descriptor_sets;
}

void noat_resources_update_uniform_buffer(NoatResources *res, uint32_t frame_index, const void *data, size_t size) {
    void *mapped;
    vkMapMemory(res->device, res->uniform_buffer_memories[frame_index], 0, size, 0, &mapped);
    memcpy(mapped, data, size);
    vkUnmapMemory(res->device, res->uniform_buffer_memories[frame_index]);
}

VkDescriptorSetLayout noat_resources_get_descriptor_set_layout(const NoatResources *res) {
    return res->descriptor_set_layout;
}