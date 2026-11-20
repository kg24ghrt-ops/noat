#include "capabilities.h"

#include <cstring>
#include <sstream>

GPUCaps GPUCapsDetector::detect() {
    GPUCaps caps;

    // Query version strings
    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
    const GLubyte* extensions = glGetString(GL_EXTENSIONS);

    if (vendor) caps.vendor = reinterpret_cast<const char*>(vendor);
    if (renderer) caps.renderer = reinterpret_cast<const char*>(renderer);
    if (version) caps.version = reinterpret_cast<const char*>(version);
    if (glsl) caps.shading_language_version = reinterpret_cast<const char*>(glsl);

    // Detect Mali
    if (!caps.renderer.empty() && caps.renderer.find("Mali") != std::string::npos) {
        caps.is_mali = true;
        // Extract model name like "Mali-G610"
        auto pos = caps.renderer.find("Mali-");
        if (pos != std::string::npos) {
            auto end = caps.renderer.find(' ', pos);
            if (end == std::string::npos) end = caps.renderer.length();
            caps.mali_model = caps.renderer.substr(pos, end - pos);
        }
    }

    // Parse OpenGL ES version (format: "OpenGL ES 3.2 ...")
    if (version) {
        std::string vstr(reinterpret_cast<const char*>(version));
        // Find "OpenGL ES X.Y"
        auto es_pos = vstr.find("OpenGL ES ");
        if (es_pos != std::string::npos) {
            auto start = es_pos + 10; // length of "OpenGL ES "
            if (start < vstr.size()) {
                caps.major = vstr[start] - '0';
                if (start + 2 < vstr.size() && vstr[start + 1] == '.') {
                    caps.minor = vstr[start + 2] - '0';
                }
            }
        }
    }

    // Parse GLSL version
    if (glsl) {
        std::string gstr(reinterpret_cast<const char*>(glsl));
        // Format: "OpenGL ES GLSL ES 3.20" or "3.20"
        auto es_pos = gstr.find("OpenGL ES GLSL ES ");
        if (es_pos != std::string::npos) {
            auto start = es_pos + 18;
            if (start < gstr.size()) {
                caps.glsl_major = gstr[start] - '0';
                if (start + 2 < gstr.size() && gstr[start + 1] == '.') {
                    caps.glsl_minor = (gstr[start + 2] - '0') * 10 + (start + 3 < gstr.size() ? gstr[start + 3] - '0' : 0);
                }
            }
        } else {
            // Plain version like "3.20"
            if (!gstr.empty()) {
                caps.glsl_major = gstr[0] - '0';
            }
        }
    }

    // Query limits
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &caps.max_texture_size);
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &caps.max_cubemap_texture_size);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &caps.max_renderbuffer_size);
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &caps.max_vertex_texture_image_units);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &caps.max_texture_image_units);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &caps.max_combined_texture_image_units);
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &caps.max_vertex_attribs);
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &caps.max_vertex_uniform_vectors);
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &caps.max_fragment_uniform_vectors);
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &caps.max_varying_vectors);

    if (caps.major >= 3 && caps.minor >= 0) {
        glGetIntegerv(GL_MAX_DRAW_BUFFERS, &caps.max_draw_buffers);
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &caps.max_color_attachments);
    }

    // Check extensions
    std::string ext_str(extensions ? reinterpret_cast<const char*>(extensions) : "");

    auto has_ext = [&ext_str](const char* name) {
        return ext_str.find(name) != std::string::npos;
    };

    // GLES 3.0 core features
    caps.has_etc2 = true; // ETC2 is core in GLES 3.0
    caps.has_invalidate_framebuffer = (caps.major >= 3 && caps.minor >= 0);
    caps.has_occlusion_query = (caps.major >= 3 && caps.minor >= 0);
    caps.has_msaa = (caps.major >= 3 && caps.minor >= 0);

    // Extension checks
    caps.has_tessellation = has_ext("GL_OES_tessellation_shader") || (caps.major >= 3 && caps.minor >= 2);
    caps.has_geometry_shader = (caps.major >= 3 && caps.minor >= 2);
    caps.has_compute_shader = has_ext("GL_OES_compute_shader") || (caps.major >= 3 && caps.minor >= 1);
    caps.has_astc = has_ext("GL_COMPRESSED_RGBA_ASTC_4x4_KHR") || (caps.major >= 3 && caps.minor >= 2);
    caps.has_framebuffer_fetch = has_ext("GL_ARM_shader_framebuffer_fetch");
    caps.has_pixel_local_storage = has_ext("GL_EXT_shader_pixel_local_storage");
    caps.has_anisotropic = has_ext("GL_EXT_texture_filter_anisotropic");

    // Determine recommended tier
    // Arm recommends avoiding tessellation on Mali tile-based GPUs.
    // Default to Tier A unless compute shaders are available.
    if (caps.has_compute_shader && caps.major >= 3 && caps.minor >= 1) {
        caps.recommended_tier = GPUCaps::Tier::B;
    }
    // Tier C (tessellation) is opt-in only, never default on Mali

    return caps;
}
