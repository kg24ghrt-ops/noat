#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include "text_document.h"

namespace noat {

class AppRenderer {
public:
    AppRenderer();
    ~AppRenderer();

    AppRenderer(const AppRenderer&) = delete;
    AppRenderer& operator=(const AppRenderer&) = delete;

    // Initialize Vulkan instance, device, VMA allocator (no surface yet).
    bool init();
    // Create surface from ANativeWindow and build swapchain/render resources.
    bool createSurface(ANativeWindow* window);
    bool resize(uint32_t width, uint32_t height);
    // Render one frame and present.
    bool renderFrame();
    // Release surface/swapchain resources (keep instance/device for reuse).
    void releaseSurface();
    // Full shutdown.
    void shutdown();

    const char* info() const;

    // Upload parameter block to the VMA uniform buffer.
    void updateParams(const void* data, size_t size);

    // ---- Text document editing ----
    // Set committed text (replaces all content). Relayouts immediately.
    void textSetDocument(const char* utf8, uint32_t byte_length);
    // Get current committed text as UTF-8.
    std::string textGetDocument() const;
    // Insert UTF-8 at cursor.
    void textInsert(const char* utf8, uint32_t byte_length);
    // Backspace at cursor.
    void textBackspace();
    // Delete forward at cursor.
    void textDeleteForward();
    // Enter (new line) at cursor.
    void textEnter();
    // Move cursor.
    void textMoveLeft();
    void textMoveRight();
    void textMoveUp();
    void textMoveDown();
    void textMoveToLineStart();
    void textMoveToLineEnd();
    // Set cursor to a specific cluster index.
    void textSetCursor(uint32_t cluster_index);
    // Get cursor as cluster index.
    uint32_t textGetCursorCluster() const;
    // Cursor access (for caret rendering on the Kotlin side).
    uint32_t textCursor() const;
    float textCursorX() const;
    float textCursorY() const;
    uint32_t textCursorLine() const;
    // IME composing text (not deformed, rendered separately).
    void textSetComposing(const char* utf8, uint32_t byte_length);
    void textClearComposing();

    // ---- Text (handwriting) GPU support ----
    // A positioned, cluster-aware glyph instance ready for the SDF atlas.
    struct GlyphInstance {
        float x, y, w, h;          // screen px (cluster warp already applied)
        float ax, ay, aw, ah;      // atlas rect px
        float glyph_id;
        float cluster_id;
        float pad0, pad1;          // std430 align to 48 bytes
    };
    static_assert(sizeof(GlyphInstance) == 48, "GlyphInstance must be 48 bytes");

    // Upload/replace the visible glyph instances. Called incrementally.
    bool textUploadInstances(const GlyphInstance* instances, uint32_t count);
    // Bind the SDF glyph atlas (RGBA8, R = signed distance).
    bool textSetAtlas(const uint8_t* rgba8, int width, int height);
    // Clear all text instances.
    void textClearInstances();

    // Deformation parameters (CPU-side, applied before upload).
    struct DeformParams {
        float warp_freq = 0.05f;
        float warp_amp = 0.5f;
        float warp_speed = 1.0f;
        float cluster_drift = 0.3f;
        float rot_variation = 0.05f;
        float size_variation = 0.08f;
        float advance_var = 0.05f;
        float ink_feather = 2.0f;
        float pressure_var = 0.3f;
    };
    void setDeformParams(const DeformParams& p);
    const DeformParams& deformParams() const { return m_deform; }

    // Caret overlay: draw a blinking caret without re-rendering document.
    // Call each frame; returns true if caret should be visible this frame.
    bool caretUpdate(float dt_seconds);
    bool caretVisible() const { return m_caret_visible; }
    // Force caret visibility (e.g. after user interaction).
    void caretResetBlink();

    // Update paper dimensions for text layout.
    void setPaperDims(float width_mm, float height_mm, float rule_gap_mm, float rule_top_mm, float margin_x_mm);

private:
    bool createInstance();
    bool selectPhysicalDevice();
    bool createDevice();
    bool createAllocator();
    bool createRenderResourcesOnce();
    bool createSwapchain();
    bool createSyncObjects();
    void destroySwapchain();
    void destroyRenderResources();
    void destroySyncObjects();

    bool createTextPipeline();
    bool createTextDescriptor();
    void destroyTextResources();
    bool updateInstanceBufferCapacity(uint32_t capacity);

    // ---- Text pipeline integration ----
    // Shape document text with HarfBuzz, bake SDF atlas, upload instances.
    bool textRelayoutAndUpload();
    // Bake SDF atlas for the glyphs needed by the current shaped run.
    bool textBakeAtlas();
    // Upload deformation params as push constants (no UBO needed).
    void textBindDeformParams();

    VkInstance m_instance{VK_NULL_HANDLE};
    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    VkQueue m_queue{VK_NULL_HANDLE};
    uint32_t m_queueFamily{UINT32_MAX};
    VkSurfaceKHR m_surface{VK_NULL_HANDLE};
    VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
    VkFormat m_surfaceFormat{VK_FORMAT_UNDEFINED};
    VkExtent2D m_surfaceExtent{};
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    std::vector<VkFramebuffer> m_swapchainFramebuffers;
    VmaAllocator m_allocator{VK_NULL_HANDLE};

    VkRenderPass m_renderPass{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
    VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};
    VkBuffer m_paramsBuffer{VK_NULL_HANDLE};
    VmaAllocation m_paramsAlloc{VK_NULL_HANDLE};

    // Text pipeline (instanced glyph quads, blends over the paper).
    VkPipeline m_textPipeline{VK_NULL_HANDLE};
    VkPipelineLayout m_textPipelineLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_textDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorPool m_textDescriptorPool{VK_NULL_HANDLE};
    VkDescriptorSet m_textDescriptorSet{VK_NULL_HANDLE};
    VkBuffer m_instanceBuffer{VK_NULL_HANDLE};
    VmaAllocation m_instanceAlloc{VK_NULL_HANDLE};
    VkBuffer m_quadVertexBuffer{VK_NULL_HANDLE};
    VmaAllocation m_quadVertexAlloc{VK_NULL_HANDLE};
    VkImage m_atlasImage{VK_NULL_HANDLE};
    VmaAllocation m_atlasAlloc{VK_NULL_HANDLE};
    VkImageView m_atlasView{VK_NULL_HANDLE};
    VkSampler m_atlasSampler{VK_NULL_HANDLE};
    int m_atlasWidth{0};
    int m_atlasHeight{0};
    uint32_t m_instanceCapacity{0};
    uint32_t m_instanceCount{0};

    // Deformation parameters (CPU-side).
    DeformParams m_deform;

    // Text document model.
    TextDocument m_document;

    // Shaped glyph data (from HarfBuzz) - used for atlas baking and positioning.
    std::vector<uint32_t> m_shaped_glyph_ids;
    std::vector<float> m_shaped_advances;
    std::vector<uint32_t> m_shaped_cluster_ids;
    bool m_atlas_dirty = true;   // atlas needs re-bake
    bool m_instances_dirty = true; // instance buffer needs re-upload

    // Caret overlay state.
    bool m_caret_visible = true;
    float m_caret_blink_timer = 0.0f;
    static constexpr float kCaretBlinkInterval = 0.5f; // seconds

    VkCommandPool m_commandPool{VK_NULL_HANDLE};
    VkCommandBuffer m_commandBuffer{VK_NULL_HANDLE};

    VkSemaphore m_imageAvailableSemaphore{VK_NULL_HANDLE};
    VkSemaphore m_renderFinishedSemaphore{VK_NULL_HANDLE};
    VkFence m_inFlightFence{VK_NULL_HANDLE};

    // Paper dimensions for text layout.
    float m_paper_width_mm = 210.0f;
    float m_paper_height_mm = 297.0f;
    float m_rule_gap_mm = 7.1f;
    float m_rule_top_mm = 25.4f;
    float m_margin_x_mm = 31.75f;

    uint32_t m_width{0};
    uint32_t m_height{0};
    bool m_surfaceCreated{false};
    bool m_swapchainCreated{false};
};

}
