#pragma once

#include <GLES3/gl3.h>
#include <string>
#include <vector>

// GPU capability detection for ARM Mali and other mobile GPUs.
// Queries the active OpenGL ES context at runtime.
struct GPUCaps {
    // API version
    int major = 0;
    int minor = 0;
    int glsl_major = 0;
    int glsl_minor = 0;

    // Renderer / vendor strings
    std::string renderer;
    std::string vendor;
    std::string version;
    std::string shading_language_version;

    // Limits
    GLint max_texture_size = 0;
    GLint max_cubemap_texture_size = 0;
    GLint max_renderbuffer_size = 0;
    GLint max_vertex_texture_image_units = 0;
    GLint max_texture_image_units = 0;
    GLint max_combined_texture_image_units = 0;
    GLint max_vertex_attribs = 0;
    GLint max_vertex_uniform_vectors = 0;
    GLint max_fragment_uniform_vectors = 0;
    GLint max_varying_vectors = 0;
    GLint max_draw_buffers = 0;
    GLint max_color_attachments = 0;

    // Extensions
    bool has_tessellation = false;          // GL_OES_tessellation_shader / GLES 3.2
    bool has_geometry_shader = false;       // GLES 3.2
    bool has_compute_shader = false;        // GLES 3.1 / AEP
    bool has_astc = false;                  // GL_COMPRESSED_RGBA_ASTC_* / AEP
    bool has_etc2 = false;                  // GLES 3.0 core
    bool has_framebuffer_fetch = false;     // ARM_shader_framebuffer_fetch
    bool has_pixel_local_storage = false;   // EXT_shader_pixel_local_storage
    bool has_invalidate_framebuffer = false;// GLES 3.0 core (glInvalidateFramebuffer)
    bool has_occlusion_query = false;       // GLES 3.0 core
    bool has_msaa = false;                  // GLES 3.0 core
    bool has_anisotropic = false;           // EXT_texture_filter_anisotropic

    // Mali-specific hints
    bool is_mali = false;
    std::string mali_model; // e.g. "Mali-G610"

    // Recommended rendering tier
    enum class Tier {
        A = 0, // GLES 3.0 baseline - no tessellation
        B = 1, // GLES 3.1 - compute shaders available
        C = 2, // GLES 3.2 - tessellation available (optional)
    };
    Tier recommended_tier = Tier::A;
};

class GPUCapsDetector {
public:
    static GPUCaps detect();
};
