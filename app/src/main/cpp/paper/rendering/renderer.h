#pragma once

#include "core/capabilities.h"
#include "core/gl_object.hpp"
#include "material/material.h"
#include "rendering/geometry.h"
#include "rendering/program.h"
#include "rendering/texture.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

// Quality levels matching the paper engine specification.
enum class QualityLevel {
    Low = 0,
    Balanced = 1,
    High = 2,
    Ultra = 3,
};

// Render configuration. Controls quality vs performance trade-offs.
struct RenderConfig {
    QualityLevel quality = QualityLevel::Balanced;
    bool enable_tessellation = false;   // Tier C - opt-in only, off by default on Mali
    bool enable_compute_prepass = false;// Tier B - compute shader for procedural textures
    bool enable_msaa = false;           // MSAA (Mali supports 4x/8x/16x with minimal cost)
    GLsizei max_subdivision = 32;       // max geometry segments per axis
    bool enable_shadows = true;
    bool enable_ink = false;
};

// Lighting parameters for paper shading (dielectric, fibrous material)
struct LightParams {
    // Directional light (sun-like)
    float direction[3] = {0.5f, -1.0f, 0.5f}; // normalized
    float color[3] = {1.0f, 1.0f, 0.95f};     // warm white
    float intensity = 1.0f;
    float ambient = 0.3f;                     // ambient fill for paper

    // Environmental illumination
    float env_color[3] = {0.8f, 0.85f, 0.9f}; // cool skylight
    float env_intensity = 0.4f;
};

// Paper material parameters (physically inspired, dielectric)
struct PaperMaterialParams {
    // Base paper color (albedo)
    float color[3] = {0.95f, 0.95f, 0.9f}; // off-white

    // Surface roughness (0 = smooth, 1 = very rough)
    float roughness = 0.35f;

    // Specular strength for paper (low - paper is mostly diffuse)
    float specular_strength = 0.1f;

    // Specular roughness (microfacet)
    float specular_roughness = 0.6f;

    // Fiber detail strength (normal map intensity)
    float fiber_strength = 0.5f;

    // Grain scale
    float grain_scale = 1.0f;

    // Thickness (for edge darkening / subsurface approximation)
    float thickness = 0.1f; // mm
};

// Main renderer. Coordinates all rendering passes for the paper engine.
// Tier A (GLES 3.0): forward rendering with normal mapping + CPU subdivision.
// Tier B (GLES 3.1): + compute shader procedural texture generation.
// Tier C (GLES 3.2): + hardware tessellation (opt-in, disabled by default).
class PaperRenderer {
public:
    PaperRenderer();
    ~PaperRenderer();

    PaperRenderer(const PaperRenderer&) = delete;
    PaperRenderer& operator=(const PaperRenderer&) = delete;

    // Initialize renderer after GLES context is current.
    // Must be called with a valid OpenGL ES context.
    void initialize(const GPUCaps& caps);

    // Set render configuration
    void setConfig(const RenderConfig& config);

    // Set lighting parameters
    void setLight(const LightParams& light);

    // Set paper material
    void setMaterial(const PaperMaterialParams& material);

    // Resize the viewport
    void resize(GLsizei width, GLsizei height);

    // Begin a frame
    void beginFrame();

    // Render one frame of paper
    void renderFrame();

    // End a frame (present)
    void endFrame();

    // Get current capabilities
    const GPUCaps& capabilities() const { return caps_; }

    // Get current quality level
    QualityLevel quality() const { return config_.quality; }

private:
    void initShaders();
    void initGeometry();
    void initTextures();
    void initFramebuffers();
    void invalidateTransientAttachments();

    void renderGeometryPass();
    void renderLightingPass();
    void renderInkPass();
    void renderCompositePass();

    GPUCaps caps_;
    RenderConfig config_;
    LightParams light_;
    PaperMaterialParams material_;

    GLsizei viewport_width_ = 0;
    GLsizei viewport_height_ = 0;

    // Shaders and programs
    std::unique_ptr<Program> paper_program_;
    std::unique_ptr<Program> lighting_program_;
    std::unique_ptr<Program> composite_program_;
    std::unique_ptr<Program> ink_program_;

    // Geometry
    Geometry paper_geom_;

    // Textures (procedural: fiber normal, grain, height)
    std::unique_ptr<Texture> fiber_normal_tex_;
    std::unique_ptr<Texture> grain_tex_;
    std::unique_ptr<Texture> height_tex_;

    // Framebuffers (transient - invalidated each frame for Mali bandwidth savings)
    GLFramebuffer gbuffer_fbo_;
    GLTexture gbuffer_color_;
    GLTexture gbuffer_normal_;
    GLTexture gbuffer_depth_;

    bool initialized_ = false;
};
