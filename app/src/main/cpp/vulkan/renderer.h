#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

class VulkanContext;
class VulkanRenderPass;
class VulkanPipeline;
class VulkanResources;
class VulkanFrame;

enum class QualityLevel {
    Low = 0,
    Balanced = 1,
    High = 2,
    Ultra = 3,
};

struct LightParams {
    float direction[3] = {0.5f, -1.0f, 0.5f};
    float color[3] = {1.0f, 1.0f, 0.95f};
    float intensity = 1.0f;
    float ambient = 0.3f;
    float envColor[3] = {0.8f, 0.85f, 0.9f};
    float envIntensity = 0.4f;
};

struct PaperMaterialParams {
    float color[3] = {0.95f, 0.95f, 0.90f};
    float roughness = 0.35f;
    float specularStrength = 0.10f;
    float specularRoughness = 0.60f;
    float fiberStrength = 0.50f;
    float grainScale = 1.0f;
    float thickness = 0.10f;
};

// Main Vulkan renderer. Orchestrates all rendering passes for the paper engine.
// Uses subpasses (geometry + lighting) to keep intermediate data in tile memory
// on Mali GPUs, minimizing external memory bandwidth.
class PaperRenderer {
public:
    PaperRenderer();
    ~PaperRenderer();

    PaperRenderer(const PaperRenderer&) = delete;
    PaperRenderer& operator=(const PaperRenderer&) = delete;

    // Initialize renderer with an Android native window
    bool init(void* nativeWindow);

    // Recreate swapchain on surface size change
    bool resize(int width, int height);

    // Set render configuration
    void setConfig(QualityLevel quality);

    // Set lighting parameters
    void setLight(const LightParams& light);

    // Set paper material
    void setMaterial(const PaperMaterialParams& material);

    // Render one frame
    void renderFrame();

    void shutdown();

    bool isInitialized() const { return initialized_; }

private:
    bool createSyncObjects();
    void destroySyncObjects();
    bool recordCommandBuffer(uint32_t frameIndex);
    bool drawFrame();

    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<VulkanRenderPass> renderPass_;
    std::unique_ptr<VulkanPipeline> pipeline_;
    std::unique_ptr<VulkanResources> resources_;
    std::vector<std::unique_ptr<VulkanFrame>> frames_;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    uint32_t currentFrame_ = 0;

    QualityLevel quality_ = QualityLevel::Balanced;
    LightParams light_;
    PaperMaterialParams material_;

    bool initialized_ = false;
};
