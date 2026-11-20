#include "resources.h"

#include <android/log.h>
#include <cstring>

#define TAG "VulkanResources"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

VulkanResources::~VulkanResources() {
    // shutdown is called explicitly
}

bool VulkanResources::init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool,
                           VkQueue queue, VkDescriptorSetLayout descriptorSetLayout,
                           uint32_t swapchainImageCount) {
    device_ = device;
    physicalDevice_ = physicalDevice;
    commandPool_ = commandPool;
    queue_ = queue;
    descriptorSetLayout_ = descriptorSetLayout;
    swapchainImageCount_ = swapchainImageCount;

    if (!createDescriptorSetLayout()) return false;
    if (!createDescriptorPool(swapchainImageCount)) return false;
    if (!createDescriptorSets(swapchainImageCount)) return false;

    // Uniform buffer size: model(16 floats) + view(16 floats) + projection(16 floats) +
    // light direction(3) + light color(3) + light intensity(1) + ambient(1) +
    // env color(3) + env intensity(1) + paper color(3) + roughness(1) +
    // specular strength(1) + specular roughness(1) + fiber strength(1) +
    // grain scale(1) + thickness(1) = ~100 floats = 400 bytes, round to 256 alignment
    VkDeviceSize bufferSize = 256;
    if (!createUniformBuffers(bufferSize)) return false;

    LOGI("Vulkan resources initialized");
    return true;
}

void VulkanResources::shutdown() {
    for (auto buf : uniformBuffers_) {
        vkDestroyBuffer(device_, buf, nullptr);
    }
    for (auto mem : uniformBufferMemories_) {
        vkFreeMemory(device_, mem, nullptr);
    }

    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
    }
}

bool VulkanResources::createDescriptorSetLayout() {
    // Uniform buffer binding
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uboBinding.pImmutableSamplers = nullptr;

    // Combined image sampler binding (for fiber normal texture)
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboBinding, samplerBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        LOGE("Failed to create descriptor set layout");
        return false;
    }
    return true;
}

bool VulkanResources::createDescriptorPool(uint32_t maxSets) {
    std::array<VkDescriptorPoolSize, 2> poolSizes = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxSets;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = maxSets;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxSets;

    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        LOGE("Failed to create descriptor pool");
        return false;
    }
    return true;
}

bool VulkanResources::createDescriptorSets(uint32_t count) {
    std::vector<VkDescriptorSetLayout> layouts(count, descriptorSetLayout_);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = count;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets_.resize(count);
    if (vkAllocateDescriptorSets(device_, &allocInfo, descriptorSets_.data()) != VK_SUCCESS) {
        LOGE("Failed to allocate descriptor sets");
        return false;
    }
    return true;
}

bool VulkanResources::createUniformBuffers(VkDeviceSize bufferSize) {
    uniformBuffers_.resize(swapchainImageCount_);  // set by caller via descriptorSets_.size()
    uniformBufferMemories_.resize(swapchainImageCount_);

    for (size_t i = 0; i < swapchainImageCount_; ++i) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device_, &bufferInfo, nullptr, &uniformBuffers_[i]) != VK_SUCCESS) {
            LOGE("Failed to create uniform buffer %zu", i);
            return false;
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device_, uniformBuffers_[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = 0;  // find coherent host-visible memory

        // Find host-visible, host-coherent memory type
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);
        for (uint32_t j = 0; j < memProperties.memoryTypeCount; ++j) {
            if ((memRequirements.memoryTypeBits & (1u << j)) &&
                (memProperties.memoryTypes[j].propertyFlags &
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
                allocInfo.memoryTypeIndex = j;
                break;
            }
        }

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &uniformBufferMemories_[i]) != VK_SUCCESS) {
            LOGE("Failed to allocate uniform buffer memory %zu", i);
            return false;
        }

        vkBindBufferMemory(device_, uniformBuffers_[i], uniformBufferMemories_[i], 0);
    }
    return true;
}

void VulkanResources::updateUniformBuffer(uint32_t frameIndex, const void* data, VkDeviceSize size) {
    void* mapped;
    vkMapMemory(device_, uniformBufferMemories_[frameIndex], 0, size, 0, &mapped);
    std::memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(device_, uniformBufferMemories_[frameIndex]);
}
