// App renderer: surface/swapchain Vulkan path for Milestone 5.
// Shares the same shader (paper_wear.frag.spv) and parameter block as the
// headless harness. Renders a fullscreen triangle into each swapchain image
// and presents. One render pass, one subpass, color attachment only, no depth,
// no MSAA, fullscreen triangle, no vertex/index buffer, loadOp DONT_CARE,
// storeOp STORE.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "renderer.h"
#include "paper_wear_spv.h"
#include "vert_spirv.inc"

#include <android/log.h>
#include <vulkan/vulkan_android.h>
#include <cstring>
#include <vector>

namespace noat {

namespace {

void logV(const char* msg) {
    __android_log_print(ANDROID_LOG_INFO, "noat", "%s", msg);
}
void logE(const char* msg) {
    __android_log_print(ANDROID_LOG_ERROR, "noat", "%s", msg);
}

} // namespace

AppRenderer::AppRenderer() = default;

AppRenderer::~AppRenderer() {
    shutdown();
}

const char* AppRenderer::info() const {
    static thread_local char buf[256];
    snprintf(buf, sizeof(buf), "Noat paper renderer (raw Vulkan, Mali-G610 target) "
                                 "surface=%dx%d",
             m_width, m_height);
    return buf;
}

bool AppRenderer::init() {
    if (!createInstance()) {
        logE("createInstance failed");
        return false;
    }
    if (!selectPhysicalDevice()) {
        logE("selectPhysicalDevice failed");
        return false;
    }
    if (!createDevice()) {
        logE("createDevice failed");
        return false;
    }
    if (!createAllocator()) {
        logE("createAllocator failed");
        return false;
    }
    if (!createRenderResourcesOnce()) {
        logE("createRenderResourcesOnce failed");
        return false;
    }
    logV("AppRenderer init complete");
    return true;
}

bool AppRenderer::createInstance() {
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "noat";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "noat";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, exts.data());

    std::vector<const char*> enabledExts = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };
#ifndef NDEBUG
    enabledExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    for (const char* req : enabledExts) {
        bool found = false;
        for (const auto& e : exts) {
            if (std::strcmp(e.extensionName, req) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            logE(("Missing instance extension: " + std::string(req)).c_str());
            return false;
        }
    }

    VkInstanceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = static_cast<uint32_t>(enabledExts.size());
    ci.ppEnabledExtensionNames = enabledExts.data();

#ifndef NDEBUG
    VkDebugUtilsMessengerCreateInfoEXT dbgCi = {};
    dbgCi.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dbgCi.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dbgCi.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dbgCi.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT,
                               VkDebugUtilsMessageTypeFlagsEXT,
                               const VkDebugUtilsMessengerCallbackDataEXT* pData,
                               void*) -> VkBool32 {
        logE(pData->pMessage);
        return VK_FALSE;
    };
    ci.pNext = &dbgCi;
#endif

    if (vkCreateInstance(&ci, nullptr, &m_instance) != VK_SUCCESS) {
        logE("vkCreateInstance failed");
        return false;
    }
    return true;
}

bool AppRenderer::selectPhysicalDevice() {
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &gpuCount, nullptr);
    if (gpuCount == 0) {
        logE("No physical devices");
        return false;
    }
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(m_instance, &gpuCount, gpus.data());

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < gpuCount; ++i) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpus[i], &props);
        logV(("GPU: " + std::string(props.deviceName)).c_str());
        const char* name = props.deviceName;
        if (std::strstr(name, "Mali-G610") != nullptr) {
            chosen = gpus[i];
            break;
        }
    }
    if (chosen == VK_NULL_HANDLE) chosen = gpus[0];

    m_physicalDevice = chosen;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    logV(("Selected: " + std::string(props.deviceName)).c_str());
    return true;
}

bool AppRenderer::createDevice() {
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qfCount, qfs.data());

    m_queueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < qfCount; ++i) {
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);
        if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && presentSupport) {
            m_queueFamily = i;
            break;
        }
    }
    if (m_queueFamily == UINT32_MAX) {
        for (uint32_t i = 0; i < qfCount; ++i) {
            if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                m_queueFamily = i;
                break;
            }
        }
    }
    if (m_queueFamily == UINT32_MAX) {
        logE("No graphics queue family");
        return false;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = m_queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    VkPhysicalDeviceFeatures features = {};
    VkDeviceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qci;
    ci.pEnabledFeatures = &features;

    const char* deviceExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = deviceExts;

    if (vkCreateDevice(m_physicalDevice, &ci, nullptr, &m_device) != VK_SUCCESS) {
        logE("vkCreateDevice failed");
        return false;
    }
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
    return true;
}

bool AppRenderer::createAllocator() {
    VmaVulkanFunctions funcs = {};
    funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    funcs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo ci = {};
    ci.physicalDevice = m_physicalDevice;
    ci.device = m_device;
    ci.instance = m_instance;
    ci.pVulkanFunctions = &funcs;
    if (vmaCreateAllocator(&ci, &m_allocator) != VK_SUCCESS) {
        logE("vmaCreateAllocator failed");
        return false;
    }
    return true;
}

bool AppRenderer::createRenderResourcesOnce() {
    // Render pass: one subpass, color only, loadOp DONT_CARE, storeOp STORE.
    VkAttachmentDescription att = {};
    att.format = VK_FORMAT_R8G8B8A8_UNORM;  // updated in createSwapchain
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &att;
    ci.subpassCount = 1;
    ci.pSubpasses = &subpass;
    if (vkCreateRenderPass(m_device, &ci, nullptr, &m_renderPass) != VK_SUCCESS) {
        logE("vkCreateRenderPass failed");
        return false;
    }

    // Descriptor set layout: binding 0 = params UBO (fragment).
    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslCi = {};
    dslCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslCi.bindingCount = 1;
    dslCi.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(m_device, &dslCi, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        logE("vkCreateDescriptorSetLayout failed");
        return false;
    }

    VkPushConstantRange pcRange = {};
    pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = 16;  // time, resolution_x, resolution_y, seed

    VkPipelineLayoutCreateInfo plCi = {};
    plCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCi.setLayoutCount = 1;
    plCi.pSetLayouts = &m_descriptorSetLayout;
    plCi.pushConstantRangeCount = 1;
    plCi.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(m_device, &plCi, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        logE("vkCreatePipelineLayout failed");
        return false;
    }

    // Shader modules.
    VkShaderModule vertMod = VK_NULL_HANDLE, fragMod = VK_NULL_HANDLE;
    {
        VkShaderModuleCreateInfo smCi = {};
        smCi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smCi.codeSize = sizeof(kVertSpirv);
        smCi.pCode = kVertSpirv;
        if (vkCreateShaderModule(m_device, &smCi, nullptr, &vertMod) != VK_SUCCESS) {
            logE("vkCreateShaderModule (vert) failed");
            return false;
        }
    }
    {
        VkShaderModuleCreateInfo smCi = {};
        smCi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smCi.codeSize = kPaperWearSpirvSize;
        smCi.pCode = kPaperWearSpirv;
        if (vkCreateShaderModule(m_device, &smCi, nullptr, &fragMod) != VK_SUCCESS) {
            logE("vkCreateShaderModule (frag) failed");
            return false;
        }
    }

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState cbAtt = {};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cbAtt.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cbAtt;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState = {};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gpCi = {};
    gpCi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpCi.stageCount = 2;
    gpCi.pStages = stages;
    gpCi.pVertexInputState = &vi;
    gpCi.pInputAssemblyState = &ia;
    gpCi.pViewportState = &vp;
    gpCi.pRasterizationState = &rs;
    gpCi.pMultisampleState = &ms;
    gpCi.pDepthStencilState = &ds;
    gpCi.pColorBlendState = &cb;
    gpCi.pDynamicState = &dynState;
    gpCi.layout = m_pipelineLayout;
    gpCi.renderPass = m_renderPass;
    gpCi.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpCi, nullptr, &m_pipeline) != VK_SUCCESS) {
        logE("vkCreateGraphicsPipelines failed");
        return false;
    }

    vkDestroyShaderModule(m_device, vertMod, nullptr);
    vkDestroyShaderModule(m_device, fragMod, nullptr);

    // Params uniform buffer (VMA, host visible).
    VkBufferCreateInfo ubufCi = {};
    ubufCi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ubufCi.size = 272;  // sizeof(Params), padded to alignas(16)
    ubufCi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    ubufCi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ubufAllocCi = {};
    ubufAllocCi.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    ubufAllocCi.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                        VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo ubufAllocInfo = {};
    if (vmaCreateBuffer(m_allocator, &ubufCi, &ubufAllocCi,
                        &m_paramsBuffer, &m_paramsAlloc, &ubufAllocInfo) != VK_SUCCESS) {
        logE("vmaCreateBuffer (params) failed");
        return false;
    }

    // Descriptor pool + set.
    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpCi = {};
    dpCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpCi.maxSets = 1;
    dpCi.poolSizeCount = 1;
    dpCi.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(m_device, &dpCi, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        logE("vkCreateDescriptorPool failed");
        return false;
    }
    VkDescriptorSetAllocateInfo dsa = {};
    dsa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsa.descriptorPool = m_descriptorPool;
    dsa.descriptorSetCount = 1;
    dsa.pSetLayouts = &m_descriptorSetLayout;
    if (vkAllocateDescriptorSets(m_device, &dsa, &m_descriptorSet) != VK_SUCCESS) {
        logE("vkAllocateDescriptorSets failed");
        return false;
    }
    VkDescriptorBufferInfo binfo = {};
    binfo.buffer = m_paramsBuffer;
    binfo.offset = 0;
    binfo.range = ubufCi.size;
    VkWriteDescriptorSet wds = {};
    wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wds.dstSet = m_descriptorSet;
    wds.dstBinding = 0;
    wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wds.pBufferInfo = &binfo;
    vkUpdateDescriptorSets(m_device, 1, &wds, 0, nullptr);

    // Command pool + buffer.
    VkCommandPoolCreateInfo poolCi = {};
    poolCi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCi.queueFamilyIndex = m_queueFamily;
    poolCi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(m_device, &poolCi, nullptr, &m_commandPool) != VK_SUCCESS) {
        logE("vkCreateCommandPool failed");
        return false;
    }
    VkCommandBufferAllocateInfo cbAi = {};
    cbAi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAi.commandPool = m_commandPool;
    cbAi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAi.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_device, &cbAi, &m_commandBuffer) != VK_SUCCESS) {
        logE("vkAllocateCommandBuffers failed");
        return false;
    }

    return true;
}

void AppRenderer::updateParams(const void* data, size_t size) {
    if (m_paramsBuffer == VK_NULL_HANDLE || m_allocator == VK_NULL_HANDLE) return;
    void* mapped;
    vmaMapMemory(m_allocator, m_paramsAlloc, &mapped);
    std::memcpy(mapped, data, size);
    vmaFlushAllocation(m_allocator, m_paramsAlloc, 0, VK_WHOLE_SIZE);
    vmaUnmapMemory(m_allocator, m_paramsAlloc);
}

bool AppRenderer::createSurface(ANativeWindow* window) {
    VkAndroidSurfaceCreateInfoKHR ci = {};
    ci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    ci.window = window;
    if (vkCreateAndroidSurfaceKHR(m_instance, &ci, nullptr, &m_surface) != VK_SUCCESS) {
        logE("vkCreateAndroidSurfaceKHR failed");
        return false;
    }
    m_surfaceCreated = true;
    return createSwapchain();
}

bool AppRenderer::createSwapchain() {
    if (!m_surfaceCreated) return false;

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, formats.data());
    VkSurfaceFormatKHR surfFmt = formats[0];
    if (fmtCount == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
        surfFmt = {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }
    m_surfaceFormat = surfFmt.format;

    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pmCount, presentModes.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = m_width ? m_width : 720;
        extent.height = m_height ? m_height : 1280;
    }
    m_surfaceExtent = extent;
    m_width = extent.width;
    m_height = extent.height;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = m_surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = m_surfaceFormat;
    ci.imageColorSpace = surfFmt.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = presentMode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = m_swapchain;

    if (vkCreateSwapchainKHR(m_device, &ci, nullptr, &m_swapchain) != VK_SUCCESS) {
        logE("vkCreateSwapchainKHR failed");
        return false;
    }

    if (ci.oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, ci.oldSwapchain, nullptr);
        for (auto iv : m_swapchainImageViews)
            vkDestroyImageView(m_device, iv, nullptr);
        for (auto fb : m_swapchainFramebuffers)
            vkDestroyFramebuffer(m_device, fb, nullptr);
        m_swapchainImageViews.clear();
        m_swapchainFramebuffers.clear();
    }

    uint32_t scCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &scCount, nullptr);
    m_swapchainImages.resize(scCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &scCount, m_swapchainImages.data());

    m_swapchainImageViews.resize(scCount);
    for (uint32_t i = 0; i < scCount; ++i) {
        VkImageViewCreateInfo vi = {};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = m_swapchainImages[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = m_surfaceFormat;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(m_device, &vi, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS) {
            logE("vkCreateImageView (swapchain) failed");
            return false;
        }
    }

    // Create framebuffers for new swapchain images.
    m_swapchainFramebuffers.resize(m_swapchainImageViews.size());
    for (size_t i = 0; i < m_swapchainImageViews.size(); ++i) {
        VkImageView attachments[] = {m_swapchainImageViews[i]};
        VkFramebufferCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = m_renderPass;
        fci.attachmentCount = 1;
        fci.pAttachments = attachments;
        fci.width = m_surfaceExtent.width;
        fci.height = m_surfaceExtent.height;
        fci.layers = 1;
        if (vkCreateFramebuffer(m_device, &fci, nullptr, &m_swapchainFramebuffers[i]) != VK_SUCCESS) {
            logE("vkCreateFramebuffer failed");
            return false;
        }
    }

    m_swapchainCreated = true;
    return createSyncObjects();
}

bool AppRenderer::createSyncObjects() {
    VkSemaphoreCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci = {};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateSemaphore(m_device, &sci, nullptr, &m_imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(m_device, &sci, nullptr, &m_renderFinishedSemaphore) != VK_SUCCESS ||
        vkCreateFence(m_device, &fci, nullptr, &m_inFlightFence) != VK_SUCCESS) {
        logE("sync object creation failed");
        return false;
    }
    return true;
}

bool AppRenderer::resize(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;
    if (m_surfaceCreated && m_swapchain != VK_NULL_HANDLE) {
        return createSwapchain();
    }
    return true;
}

bool AppRenderer::renderFrame() {
    if (!m_swapchainCreated || m_device == VK_NULL_HANDLE) return false;

    vkWaitForFences(m_device, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_device, 1, &m_inFlightFence);

    uint32_t imageIndex = 0;
    VkResult res = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                         m_imageAvailableSemaphore,
                                         VK_NULL_HANDLE, &imageIndex);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        return createSwapchain();
    }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        logE("vkAcquireNextImageKHR failed");
        return false;
    }

    VkCommandBufferBeginInfo cbi = {};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(m_commandBuffer, &cbi) != VK_SUCCESS) {
        logE("vkBeginCommandBuffer failed");
        return false;
    }

    VkClearValue clearColor = {{0.0f, 0.0f, 0.0f, 0.0f}};
    VkRenderPassBeginInfo rpBi = {};
    rpBi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBi.renderPass = m_renderPass;
    rpBi.framebuffer = m_swapchainFramebuffers[imageIndex];
    rpBi.renderArea = {{0, 0}, {m_surfaceExtent.width, m_surfaceExtent.height}};
    rpBi.clearValueCount = 1;
    rpBi.pClearValues = &clearColor;
    vkCmdBeginRenderPass(m_commandBuffer, &rpBi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    // Push constants: time=0, resolution, seed=4321 (deterministic).
    float pc[4] = {0.0f, (float)m_surfaceExtent.width,
                   (float)m_surfaceExtent.height, 4321.0f};
    vkCmdPushConstants(m_commandBuffer, m_pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc);

    VkViewport viewport = {0.0f, 0.0f,
                           (float)m_surfaceExtent.width,
                           (float)m_surfaceExtent.height, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {m_surfaceExtent.width, m_surfaceExtent.height}};
    vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
    vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);

    vkCmdEndRenderPass(m_commandBuffer);

    if (vkEndCommandBuffer(m_commandBuffer) != VK_SUCCESS) {
        logE("vkEndCommandBuffer failed");
        return false;
    }

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &m_imageAvailableSemaphore;
    si.pWaitDstStageMask = waitStages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &m_commandBuffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &m_renderFinishedSemaphore;
    if (vkQueueSubmit(m_queue, 1, &si, m_inFlightFence) != VK_SUCCESS) {
        logE("vkQueueSubmit failed");
        return false;
    }

    VkPresentInfoKHR pi = {};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &m_renderFinishedSemaphore;
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_swapchain;
    pi.pImageIndices = &imageIndex;
    res = vkQueuePresentKHR(m_queue, &pi);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        return createSwapchain();
    }
    if (res != VK_SUCCESS) {
        logE("vkQueuePresentKHR failed");
        return false;
    }
    return true;
}

void AppRenderer::releaseSurface() {
    vkDeviceWaitIdle(m_device);
    destroySyncObjects();
    for (auto iv : m_swapchainImageViews)
        vkDestroyImageView(m_device, iv, nullptr);
    m_swapchainImageViews.clear();
    for (auto fb : m_swapchainFramebuffers)
        vkDestroyFramebuffer(m_device, fb, nullptr);
    m_swapchainFramebuffers.clear();
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    m_surfaceCreated = false;
    m_swapchainCreated = false;
}

void AppRenderer::shutdown() {
    vkDeviceWaitIdle(m_device);
    releaseSurface();
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    if (m_paramsBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_paramsBuffer, m_paramsAlloc);
        m_paramsBuffer = VK_NULL_HANDLE;
        m_paramsAlloc = VK_NULL_HANDLE;
    }
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
    m_physicalDevice = VK_NULL_HANDLE;
    m_queue = VK_NULL_HANDLE;
    m_queueFamily = UINT32_MAX;
    m_width = 0;
    m_height = 0;
}

} // namespace noat
