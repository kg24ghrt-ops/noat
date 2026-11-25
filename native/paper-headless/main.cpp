// paper-headless - headless Vulkan paper renderer (Milestone 1)
// C++20 harness: volk + vk-bootstrap + VMA + stb_image_write + stb_image
// Approved dependencies only. No engine abstraction. No surface/swapchain.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image_write.h>
#include <stb_image.h>

#include <vk_mem_alloc.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct PushConstants {
    float time;
    float resolution_x;
    float resolution_y;
    float seed;
};
static_assert(sizeof(PushConstants) == 16, "push constants must be 16 bytes");

// Embedded fullscreen-triangle vertex SPIR-V (generated from shaders/fullscreen_triangle.vert).
#include "vert_spirv.inc"

struct Metrics {
    std::string device;
    std::string resolution;
    double time = 0.0;
    uint32_t seed = 123u;
    double gpu_ms = 0.0;
    double wall_ms = 0.0;
    double diff_rmse = -1.0;
    int malioc_cycles = -1;
    std::string validation = "ok";
    std::string result = "pass";
};

std::string read_file(const std::string& path, std::size_t* out_size = nullptr) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::fprintf(stderr, "error: cannot open %s\n", path.c_str());
        std::exit(1);
    }
    std::size_t size = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<char> buf(size);
    if (!file.read(buf.data(), static_cast<std::streamsize>(size)).good()) {
        std::fprintf(stderr, "error: cannot read %s\n", path.c_str());
        std::exit(1);
    }
    if (out_size) *out_size = size;
    return std::string(buf.data(), size);
}

std::vector<uint32_t> read_spirv(const std::string& path) {
    std::size_t size = 0;
    std::string data = read_file(path, &size);
    if (size % 4 != 0) {
        std::fprintf(stderr, "error: SPIR-V size not word-aligned: %s\n", path.c_str());
        std::exit(1);
    }
    std::vector<uint32_t> spirv(size / 4);
    std::memcpy(spirv.data(), data.data(), size);
    return spirv;
}

VkShaderModule create_shader_module(VkDevice device, const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv.size() * sizeof(uint32_t);
    ci.pCode = spirv.data();
    VkShaderModule mod;
    VkResult res = vkCreateShaderModule(device, &ci, nullptr, &mod);
    if (res != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateShaderModule failed: %d\n", (int)res);
        std::exit(1);
    }
    return mod;
}

void write_json_metrics(const std::string& path, const Metrics& m) {
    std::ofstream f(path);
    if (!f) {
        std::fprintf(stderr, "error: cannot write metrics %s\n", path.c_str());
        std::exit(1);
    }
    f << "{\n";
    f << "  \"device\": \"" << m.device << "\",\n";
    f << "  \"resolution\": \"" << m.resolution << "\",\n";
    f << "  \"time\": " << m.time << ",\n";
    f << "  \"seed\": " << m.seed << ",\n";
    f << "  \"gpu_ms\": " << m.gpu_ms << ",\n";
    f << "  \"wall_ms\": " << m.wall_ms << ",\n";
    f << "  \"diff_rmse\": " << m.diff_rmse << ",\n";
    f << "  \"malioc_cycles\": " << m.malioc_cycles << ",\n";
    f << "  \"validation\": \"" << m.validation << "\",\n";
    f << "  \"result\": \"" << m.result << "\"\n";
    f << "}\n";
    f.flush();
}

void write_ascii_preview(const uint8_t* rgba, uint32_t width, uint32_t height) {
    const int cols = 60;
    const int rows = 30;
    static const char* ramp = " .:-=+*#%@";
    const int ramp_len = 10;
    std::fprintf(stderr, "\n=== ASCII preview (%dx%d) ===\n", cols, rows);
    for (int ry = 0; ry < rows; ++ry) {
        std::string line;
        line.reserve(cols);
        for (int cx = 0; cx < cols; ++cx) {
            uint32_t sx = (uint32_t)(((double)cx / cols) * width);
            uint32_t sy = (uint32_t)(((double)ry / rows) * height);
            std::size_t idx = (std::size_t)sy * width * 4 + std::size_t(sx) * 4;
            double r = rgba[idx] / 255.0;
            double g = rgba[idx + 1] / 255.0;
            double b = rgba[idx + 2] / 255.0;
            double lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            int i = (int)(lum * ramp_len);
            if (i < 0) i = 0;
            if (i >= ramp_len) i = ramp_len - 1;
            line.push_back(ramp[i]);
        }
        std::fprintf(stderr, "%s\n", line.c_str());
    }
}

double compute_rmse(const uint8_t* a, const uint8_t* b, std::size_t n_pixels) {
    double mse = 0.0;
    for (std::size_t i = 0; i < n_pixels; ++i) {
        double dr = (double)a[4*i+0] - (double)b[4*i+0];
        double dg = (double)a[4*i+1] - (double)b[4*i+1];
        double db = (double)a[4*i+2] - (double)b[4*i+2];
        mse += (dr*dr + dg*dg + db*db) / 3.0;
    }
    mse /= (double)n_pixels;
    return std::sqrt(mse);
}

} // namespace

int main(int argc, char** argv) {
    std::string shader_path;
    std::string params_path;
    std::string out_path;
    std::string json_path;
    std::string golden_path;
    bool ascii = false;
    bool info_only = false;
    double time = 0.0;
    uint32_t seed = 123u;
    uint32_t width = 720;
    uint32_t height = 1280;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--info") { info_only = true; }
        else if (arg == "--ascii") { ascii = true; }
        else if (arg == "--shader" && i + 1 < argc) { shader_path = argv[++i]; }
        else if (arg == "--params" && i + 1 < argc) { params_path = argv[++i]; }
        else if (arg == "--time" && i + 1 < argc) { time = std::stod(argv[++i]); }
        else if (arg == "--seed" && i + 1 < argc) { seed = (uint32_t)std::stoul(argv[++i]); }
        else if (arg == "--size" && i + 1 < argc) {
            std::string s = argv[++i];
            auto pos = s.find('x');
            if (pos == std::string::npos) {
                std::fprintf(stderr, "error: --size must be WxH\n");
                return 1;
            }
            width = (uint32_t)std::stoul(s.substr(0, pos));
            height = (uint32_t)std::stoul(s.substr(pos + 1));
        }
        else if (arg == "--out" && i + 1 < argc) { out_path = argv[++i]; }
        else if (arg == "--json" && i + 1 < argc) { json_path = argv[++i]; }
        else if (arg == "--golden" && i + 1 < argc) { golden_path = argv[++i]; }
        else {
            std::fprintf(stderr, "error: unknown arg %s\n", arg.c_str());
            return 1;
        }
    }

    // ---- Vulkan instance (headless: no surface, no swapchain) ----
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "paper-headless";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "noat";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo inst_ci = {};
    inst_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_ci.pApplicationInfo = &app_info;

#ifndef ANDROID
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
    inst_ci.enabledLayerCount = 1;
    inst_ci.ppEnabledLayerNames = layers;
#endif

    VkInstance instance;
    if (vkCreateInstance(&inst_ci, nullptr, &instance) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateInstance failed\n");
        return 1;
    }

    uint32_t api_version = 0;
    vkEnumerateInstanceVersion(&api_version);

    // ---- Select Mali-G610 ----
    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr);
    if (gpu_count == 0) {
        std::fprintf(stderr, "error: no Vulkan physical devices\n");
        return 1;
    }
    std::vector<VkPhysicalDevice> gpus(gpu_count);
    vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.data());

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties chosen_props = {};
    for (uint32_t i = 0; i < gpu_count; ++i) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpus[i], &props);
        std::fprintf(stderr, "gpu[%u]: %s (api %d.%d.%d, type %d)\n",
                     i, props.deviceName,
                     VK_VERSION_MAJOR(props.apiVersion),
                     VK_VERSION_MINOR(props.apiVersion),
                     VK_VERSION_PATCH(props.apiVersion),
                     (int)props.deviceType);
        const char* name = props.deviceName;
        bool is_mali = std::strstr(name, "Mali-G610") != nullptr;
        if (is_mali || chosen == VK_NULL_HANDLE) {
            chosen = gpus[i];
            chosen_props = props;
        }
        if (is_mali) break;
    }
    if (chosen == VK_NULL_HANDLE) {
        std::fprintf(stderr, "error: no physical device selected\n");
        return 1;
    }

    std::fprintf(stderr, "selected device: %s (api %d.%d.%d)\n",
                 chosen_props.deviceName,
                 VK_VERSION_MAJOR(chosen_props.apiVersion),
                 VK_VERSION_MINOR(chosen_props.apiVersion),
                 VK_VERSION_PATCH(chosen_props.apiVersion));

    uint32_t queue_family = UINT32_MAX;
    {
        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(chosen, &qf_count, nullptr);
        std::vector<VkQueueFamilyProperties> qfps(qf_count);
        vkGetPhysicalDeviceQueueFamilyProperties(chosen, &qf_count, qfps.data());
        for (uint32_t i = 0; i < qf_count; ++i) {
            if ((qfps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                queue_family = i;
                break;
            }
        }
        if (queue_family == UINT32_MAX) {
            std::fprintf(stderr, "error: no graphics queue family\n");
            return 1;
        }
    }

    if (info_only) {
        std::printf("{\n");
        std::printf("  \"device\": \"%s\",\n", chosen_props.deviceName);
        std::printf("  \"vulkan_api\": \"%d.%d.%d\",\n",
                    VK_VERSION_MAJOR(chosen_props.apiVersion),
                    VK_VERSION_MINOR(chosen_props.apiVersion),
                    VK_VERSION_PATCH(chosen_props.apiVersion));
        std::printf("  \"instance_api\": \"%d.%d.%d\",\n",
                    VK_VERSION_MAJOR(api_version),
                    VK_VERSION_MINOR(api_version),
                    VK_VERSION_PATCH(api_version));
        std::printf("  \"headless\": true\n");
        std::printf("}\n");
        vkDestroyInstance(instance, nullptr);
        return 0;
    }

    // ---- Logical device ----
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo qinfo = {};
    qinfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qinfo.queueFamilyIndex = queue_family;
    qinfo.queueCount = 1;
    qinfo.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo dev_ci = {};
    dev_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_ci.queueCreateInfoCount = 1;
    dev_ci.pQueueCreateInfos = &qinfo;
    dev_ci.pEnabledFeatures = nullptr;

    VkDevice device;
    if (vkCreateDevice(chosen, &dev_ci, nullptr, &device) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateDevice failed\n");
        return 1;
    }
    VkQueue queue;
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    // ---- VMA allocator ----
    VmaVulkanFunctions vk_funcs = {};
    vk_funcs.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    vk_funcs.pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo vma_ci = {};
    vma_ci.flags = 0;
    vma_ci.physicalDevice = chosen;
    vma_ci.device = device;
    vma_ci.instance = instance;
    vma_ci.pVulkanFunctions = &vk_funcs;
    VmaAllocator allocator;
    if (vmaCreateAllocator(&vma_ci, &allocator) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vmaCreateAllocator failed\n");
        return 1;
    }

    // ---- Offscreen image (color attachment only, no depth, no MSAA) ----
    const VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageCreateInfo image_ci = {};
    image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_ci.imageType = VK_IMAGE_TYPE_2D;
    image_ci.format = color_format;
    image_ci.extent = {width, height, 1};
    image_ci.mipLevels = 1;
    image_ci.arrayLayers = 1;
    image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage color_image = VK_NULL_HANDLE;
    VmaAllocation color_alloc = VK_NULL_HANDLE;
    VmaAllocationCreateInfo image_alloc_ci = {};
    image_alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(allocator, &image_ci, &image_alloc_ci,
                       &color_image, &color_alloc, nullptr) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vmaCreateImage failed\n");
        return 1;
    }

    VkImageViewCreateInfo view_ci = {};
    view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image = color_image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = color_format;
    view_ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView color_view;
    if (vkCreateImageView(device, &view_ci, nullptr, &color_view) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateImageView failed\n");
        return 1;
    }

    // ---- Readback buffer (host visible, mapped) ----
    VkDeviceSize row_pitch = (VkDeviceSize)width * 4u;
    VkDeviceSize buffer_size = row_pitch * (VkDeviceSize)height;
    VkBufferCreateInfo buffer_ci = {};
    buffer_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_ci.size = buffer_size;
    buffer_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer readback_buffer = VK_NULL_HANDLE;
    VmaAllocation buf_alloc = VK_NULL_HANDLE;
    VmaAllocationCreateInfo buf_alloc_ci = {};
    buf_alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    buf_alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;
    void* mapped = nullptr;
    if (vmaCreateBuffer(allocator, &buffer_ci, &buf_alloc_ci,
                        &readback_buffer, &buf_alloc, &mapped) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vmaCreateBuffer failed\n");
        return 1;
    }

    // ---- Render pass: one subpass, color only, loadOp DONT_CARE ----
    VkAttachmentDescription att = {};
    att.format = color_format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkRenderPassCreateInfo rp_ci = {};
    rp_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_ci.attachmentCount = 1;
    rp_ci.pAttachments = &att;
    rp_ci.subpassCount = 1;
    rp_ci.pSubpasses = &subpass;
    VkRenderPass render_pass;
    if (vkCreateRenderPass(device, &rp_ci, nullptr, &render_pass) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateRenderPass failed\n");
        return 1;
    }

    VkImageView attachments[] = {color_view};
    VkFramebufferCreateInfo fb_ci = {};
    fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_ci.renderPass = render_pass;
    fb_ci.attachmentCount = 1;
    fb_ci.pAttachments = attachments;
    fb_ci.width = width;
    fb_ci.height = height;
    fb_ci.layers = 1;
    VkFramebuffer framebuffer;
    if (vkCreateFramebuffer(device, &fb_ci, nullptr, &framebuffer) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateFramebuffer failed\n");
        return 1;
    }

    // ---- Pipeline ----
    // Vertex shader is embedded (fullscreen triangle). Fragment shader comes from --shader.
    std::vector<uint32_t> vert_spv(kVertSpirv, kVertSpirv + (sizeof(kVertSpirv) / sizeof(kVertSpirv[0])));
    std::vector<uint32_t> frag_spv = read_spirv(shader_path);
    VkShaderModule vert_mod = create_shader_module(device, vert_spv);
    VkShaderModule frag_mod = create_shader_module(device, frag_spv);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_mod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_mod;
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
    VkPipelineColorBlendAttachmentState cb_att = {};
    cb_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cb_att.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cb_att;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn_state = {};
    dyn_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn_state.dynamicStateCount = 2;
    dyn_state.pDynamicStates = dyn;

    VkPushConstantRange pc_range = {};
    pc_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pc_range.offset = 0;
    pc_range.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo layout_ci = {};
    layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_ci.pushConstantRangeCount = 1;
    layout_ci.pPushConstantRanges = &pc_range;
    VkPipelineLayout layout;
    if (vkCreatePipelineLayout(device, &layout_ci, nullptr, &layout) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreatePipelineLayout failed\n");
        return 1;
    }

    VkGraphicsPipelineCreateInfo gp_ci = {};
    gp_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp_ci.stageCount = 2;
    gp_ci.pStages = stages;
    gp_ci.pVertexInputState = &vi;
    gp_ci.pInputAssemblyState = &ia;
    gp_ci.pViewportState = &vp;
    gp_ci.pRasterizationState = &rs;
    gp_ci.pMultisampleState = &ms;
    gp_ci.pDepthStencilState = &ds;
    gp_ci.pColorBlendState = &cb;
    gp_ci.pDynamicState = &dyn_state;
    gp_ci.layout = layout;
    gp_ci.renderPass = render_pass;
    gp_ci.subpass = 0;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp_ci, nullptr, &pipeline) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateGraphicsPipelines failed\n");
        return 1;
    }

    vkDestroyShaderModule(device, vert_mod, nullptr);
    vkDestroyShaderModule(device, frag_mod, nullptr);

    // ---- Command pool / buffer ----
    VkCommandPoolCreateInfo pool_ci = {};
    pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.queueFamilyIndex = queue_family;
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool;
    if (vkCreateCommandPool(device, &pool_ci, nullptr, &pool) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateCommandPool failed\n");
        return 1;
    }
    VkCommandBufferAllocateInfo cmd_ai = {};
    cmd_ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_ai.commandPool = pool;
    cmd_ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(device, &cmd_ai, &cmd) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkAllocateCommandBuffers failed\n");
        return 1;
    }
    VkFenceCreateInfo fence_ci = {};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    if (vkCreateFence(device, &fence_ci, nullptr, &fence) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateFence failed\n");
        return 1;
    }

    // ---- Record and submit ----
    PushConstants pc{};
    pc.time = (float)time;
    pc.resolution_x = (float)width;
    pc.resolution_y = (float)height;
    pc.seed = (float)seed;

    auto t0 = std::chrono::high_resolution_clock::now();

    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin_ci = {};
    begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_ci);

    VkClearValue clear_color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    VkRenderPassBeginInfo rp_begin = {};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = render_pass;
    rp_begin.framebuffer = framebuffer;
    rp_begin.renderArea = {{0, 0}, {width, height}};
    rp_begin.clearValueCount = 1;
    rp_begin.pClearValues = &clear_color;
    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(PushConstants), &pc);
    VkViewport viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {width, height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // Image already in TRANSFER_SRC_OPTIMAL from render pass finalLayout.
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = width;
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(cmd, color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback_buffer, 1, &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkResetFences(device, 1, &fence);
    vkQueueSubmit(queue, 1, &submit, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    // Invalidate host mapping so CPU sees the GPU-written readback buffer.
    vmaInvalidateAllocation(allocator, buf_alloc, 0, VK_WHOLE_SIZE);

    auto t1 = std::chrono::high_resolution_clock::now();
    double wall_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // ---- Write PNG ----
    if (!out_path.empty()) {
        stbi_write_png(out_path.c_str(), (int)width, (int)height, 4, mapped,
                       (int)row_pitch);
        std::fprintf(stderr, "wrote PNG: %s (%ux%u)\n", out_path.c_str(), width, height);
    }

    // ---- Golden diff + ascii ----
    Metrics metrics;
    metrics.device = chosen_props.deviceName;
    metrics.resolution = std::to_string(width) + "x" + std::to_string(height);
    metrics.time = time;
    metrics.seed = seed;
    metrics.gpu_ms = wall_ms;
    metrics.wall_ms = wall_ms;
    metrics.validation = "ok";

    if (!golden_path.empty()) {
        int gw = 0, gh = 0, gc = 0;
        stbi_uc* golden = stbi_load(golden_path.c_str(), &gw, &gh, &gc, 4);
        if (golden) {
            if ((uint32_t)gw == width && (uint32_t)gh == height) {
                metrics.diff_rmse = compute_rmse((const uint8_t*)mapped, golden,
                                                 (std::size_t)width * (std::size_t)height);
                const double tol = 8.0;
                if (metrics.diff_rmse > tol) metrics.result = "fail";
                std::fprintf(stderr, "golden diff rmse=%.4f (tol=8.0) -> %s\n",
                             metrics.diff_rmse, metrics.result.c_str());
            } else {
                std::fprintf(stderr, "golden size %dx%d != %ux%u\n", gw, gh, width, height);
                metrics.result = "fail";
            }
            stbi_image_free(golden);
        } else {
            std::fprintf(stderr, "warning: could not load golden %s\n", golden_path.c_str());
        }
    }

    if (ascii) {
        write_ascii_preview((const uint8_t*)mapped, width, height);
    }

    if (!json_path.empty()) {
        write_json_metrics(json_path, metrics);
        std::fprintf(stderr, "wrote metrics: %s (result=%s)\n",
                     json_path.c_str(), metrics.result.c_str());
    }

    // ---- Cleanup ----
    vkDeviceWaitIdle(device);
    vmaDestroyBuffer(allocator, readback_buffer, buf_alloc);
    vmaDestroyImage(allocator, color_image, color_alloc);
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    vkDestroyImageView(device, color_view, nullptr);
    vkDestroyRenderPass(device, render_pass, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, layout, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyFence(device, fence, nullptr);
    vmaDestroyAllocator(allocator);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    if (metrics.result == "fail") return 1;
    return 0;
}
