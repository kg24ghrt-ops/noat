#include "rendering/renderer.h"

#include <cstring>
#include <sstream>

// GLSL ES shader sources for the paper renderer.
// All shaders use precision qualifiers appropriate for Mali GPUs:
//   mediump for colors, normals, lighting
//   highp for positions, texture coordinates where needed

namespace {

const char* kPaperVertexShader = R"""(
#version 300 es
precision highp float;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 v_position;
out vec3 v_normal;
out vec2 v_texcoord;

void main() {
    vec4 world_pos = u_model * vec4(a_position, 1.0);
    v_position = world_pos.xyz;
    v_normal = mat3(u_model) * a_normal;
    v_texcoord = a_texcoord;
    gl_Position = u_projection * u_view * world_pos;
}
)""";

const char* kPaperFragmentShader = R"""(
#version 300 es
precision mediump float;

in vec3 v_position;
in vec3 v_normal;
in vec2 v_texcoord;

uniform sampler2D u_fiber_normal;
uniform sampler2D u_grain;

uniform vec3 u_light_dir;
uniform vec3 u_light_color;
uniform float u_light_intensity;
uniform float u_ambient;
uniform vec3 u_env_color;
uniform float u_env_intensity;
uniform vec3 u_paper_color;
uniform float u_roughness;
uniform float u_specular_strength;
uniform float u_specular_roughness;
uniform float u_fiber_strength;
uniform float u_grain_scale;
uniform float u_thickness;

out vec4 frag_color;

void main() {
    // Sample fiber normal map (tiling)
    vec2 fiber_uv = v_texcoord * 8.0;
    vec3 fiber_normal = texture(u_fiber_normal, fiber_uv).rgb;
    fiber_normal = fiber_normal * 2.0 - 1.0;

    // Sample grain texture
    vec2 grain_uv = v_texcoord * u_grain_scale * 4.0;
    float grain = texture(u_grain, grain_uv).r;

    // Combine normals: base + fiber detail
    vec3 normal = normalize(v_normal + fiber_normal * u_fiber_strength * 0.5);
    normal = normalize(normal + vec3(grain * 0.1, grain * 0.1, 0.0));

    // Diffuse (Lambertian) - paper is primarily diffuse
    float ndotl = max(dot(normal, u_light_dir), 0.0);
    vec3 diffuse = u_paper_color * u_light_color * u_light_intensity * ndotl;

    // Ambient + environmental
    vec3 ambient = u_paper_color * u_env_color * u_ambient * u_env_intensity;

    // Specular (Blinn-Phong approximation, low strength for paper)
    vec3 view_dir = normalize(-v_position);
    vec3 half_dir = normalize(u_light_dir + view_dir);
    float ndoth = max(dot(normal, half_dir), 0.0);
    float specular = pow(ndoth, 16.0 * (1.0 - u_specular_roughness) + 1.0);
    vec3 spec = u_light_color * u_light_intensity * u_specular_strength * specular;

    // Edge darkening (thickness approximation - subsurface scattering hint)
    float edge = 1.0 - max(dot(normal, view_dir), 0.0);
    vec3 edge_dark = u_paper_color * 0.3 * edge * u_thickness * 5.0;

    // Combine
    vec3 color = diffuse + ambient + spec - edge_dark;
    color += grain * 0.05; // subtle grain in final color

    frag_color = vec4(color, 1.0);
}
)""";

const char* kCompositeVertexShader = R"""(
#version 300 es
precision highp float;

layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_texcoord;

out vec2 v_texcoord;

void main() {
    v_texcoord = a_texcoord;
    gl_Position = vec4(a_position * 2.0 - 1.0, 0.0, 1.0);
}
)""";

const char* kCompositeFragmentShader = R"""(
#version 300 es
precision mediump float;

in vec2 v_texcoord;
uniform sampler2D u_scene;
out vec4 frag_color;

void main() {
    frag_color = texture(u_scene, v_texcoord);
}
)""";

// Full-screen quad for composite pass
const float kQuadVertices[] = {
    -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
};
const GLuint kQuadIndices[] = {0, 1, 2, 2, 1, 3};

} // namespace

PaperRenderer::PaperRenderer() = default;

PaperRenderer::~PaperRenderer() = default;

void PaperRenderer::initialize(const GPUCaps& caps) {
    caps_ = caps;
    initialized_ = true;

    initShaders();
    initGeometry();
    initTextures();
    initFramebuffers();
}

void PaperRenderer::setConfig(const RenderConfig& config) {
    config_ = config;
}

void PaperRenderer::setLight(const LightParams& light) {
    light_ = light;
}

void PaperRenderer::setMaterial(const PaperMaterialParams& material) {
    material_ = material;
}

void PaperRenderer::resize(GLsizei width, GLsizei height) {
    viewport_width_ = width;
    viewport_height_ = height;
    glViewport(0, 0, width, height);
    initFramebuffers();
}

void PaperRenderer::beginFrame() {
    if (!initialized_) return;
    // Invalidate transient attachments to save Mali tile-memory bandwidth
    invalidateTransientAttachments();
}

void PaperRenderer::invalidateTransientAttachments() {
    if (gbuffer_fbo_.valid()) {
        glBindFramebuffer(GL_FRAMEBUFFER, gbuffer_fbo_.get());
        GLenum attachments[] = {
            GL_COLOR_ATTACHMENT0,
            GL_COLOR_ATTACHMENT1,
            GL_DEPTH_ATTACHMENT,
        };
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 3, attachments);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

void PaperRenderer::initShaders() {
    // Paper shader (geometry + lighting combined in one forward pass for Tier A)
    Shader vert(Shader::Type::Vertex, kPaperVertexShader);
    Shader frag(Shader::Type::Fragment, kPaperFragmentShader);
    paper_program_ = std::make_unique<Program>(std::vector<Shader>{std::move(vert), std::move(frag)});

    // Composite shader (full-screen quad)
    Shader comp_vert(Shader::Type::Vertex, kCompositeVertexShader);
    Shader comp_frag(Shader::Type::Fragment, kCompositeFragmentShader);
    composite_program_ = std::make_unique<Program>(std::vector<Shader>{std::move(comp_vert), std::move(comp_frag)});
}

void PaperRenderer::initGeometry() {
    // Determine subdivision based on quality level
    GLsizei seg_x = 16, seg_y = 16;
    switch (config_.quality) {
        case QualityLevel::Low: seg_x = seg_y = 16; break;
        case QualityLevel::Balanced: seg_x = seg_y = 32; break;
        case QualityLevel::High: seg_x = seg_y = 64; break;
        case QualityLevel::Ultra: seg_x = seg_y = 128; break;
    }
    // Cap at max_subdivision
    if (seg_x > config_.max_subdivision) seg_x = config_.max_subdivision;
    if (seg_y > config_.max_subdivision) seg_y = config_.max_subdivision;

    paper_geom_ = makePaperPlane(seg_x, seg_y, 1.0f, 1.0f);
}

void PaperRenderer::initTextures() {
    // Create procedural fiber normal map (tiling, 256x256 RGBA8)
    // In production this would be generated via compute shader (Tier B)
    // or loaded from ASTC-compressed asset. For now, generate a simple
    // procedural noise pattern on the CPU.
    const int tex_size = 256;
    std::vector<uint8_t> normal_data(tex_size * tex_size * 4, 128);
    std::vector<uint8_t> grain_data(tex_size * tex_size * 4, 128);

    // Simple pseudo-noise for fiber normals
    for (int y = 0; y < tex_size; ++y) {
        for (int x = 0; x < tex_size; ++x) {
            size_t idx = (y * tex_size + x) * 4;
            // Deterministic pseudo-random
            float fx = static_cast<float>(x) / tex_size;
            float fy = static_cast<float>(y) / tex_size;
            float noise = std::sin(fx * 12.9898f + fy * 78.233f) * 43758.5453f;
            noise = noise - std::floor(noise);
            float noise2 = std::sin(fx * 3.14159f * 5.0f + fy * 2.718f * 3.0f) * 43758.5453f;
            noise2 = noise2 - std::floor(noise2);

            normal_data[idx + 0] = static_cast<uint8_t>(128 + noise * 127.0f);
            normal_data[idx + 1] = static_cast<uint8_t>(128 + noise2 * 127.0f);
            normal_data[idx + 2] = 255;
            normal_data[idx + 3] = 255;

            grain_data[idx + 0] = static_cast<uint8_t>(128 + noise * 40.0f);
            grain_data[idx + 1] = static_cast<uint8_t>(128 + noise * 40.0f);
            grain_data[idx + 2] = static_cast<uint8_t>(128 + noise * 40.0f);
            grain_data[idx + 3] = 255;
        }
    }

    fiber_normal_tex_ = std::make_unique<Texture>(tex_size, tex_size, Texture::Format::RGBA8,
                                                  normal_data.data(), true);
    fiber_normal_tex_->setWrapMode(Texture::WrapMode::Repeat, Texture::WrapMode::Repeat);
    fiber_normal_tex_->setFilter(Texture::Filter::Linear, Texture::Filter::Linear);

    grain_tex_ = std::make_unique<Texture>(tex_size, tex_size, Texture::Format::RGBA8,
                                           grain_data.data(), true);
    grain_tex_->setWrapMode(Texture::WrapMode::Repeat, Texture::WrapMode::Repeat);
    grain_tex_->setFilter(Texture::Filter::Linear, Texture::Filter::Linear);
}

void PaperRenderer::initFramebuffers() {
    // Destroy old framebuffer if any
    gbuffer_fbo_ = GLFramebuffer{};
    gbuffer_color_ = GLTexture{};
    gbuffer_normal_ = GLTexture{};
    gbuffer_depth_ = GLTexture{};

    if (viewport_width_ == 0 || viewport_height_ == 0) return;

    // Create color texture (RGBA8)
    GLuint color_tex = 0;
    glGenTextures(1, &color_tex);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, viewport_width_, viewport_height_,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gbuffer_color_.create(color_tex);

    // Create normal texture (RGBA8)
    GLuint normal_tex = 0;
    glGenTextures(1, &normal_tex);
    glBindTexture(GL_TEXTURE_2D, normal_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, viewport_width_, viewport_height_,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gbuffer_normal_.create(normal_tex);

    // Create depth texture
    GLuint depth_tex = 0;
    glGenTextures(1, &depth_tex);
    glBindTexture(GL_TEXTURE_2D, depth_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, viewport_width_, viewport_height_,
                 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gbuffer_depth_.create(depth_tex);

    // Create framebuffer
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, gbuffer_color_.get(), 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                           GL_TEXTURE_2D, gbuffer_normal_.get(), 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, gbuffer_depth_.get(), 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        // Framebuffer incomplete - fall back to default framebuffer
        gbuffer_fbo_ = GLFramebuffer{};
        gbuffer_color_ = GLTexture{};
        gbuffer_normal_ = GLTexture{};
        gbuffer_depth_ = GLTexture{};
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        gbuffer_fbo_.create(fbo);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PaperRenderer::renderGeometryPass() {
    // Bind G-buffer or default framebuffer
    if (gbuffer_fbo_.valid()) {
        glBindFramebuffer(GL_FRAMEBUFFER, gbuffer_fbo_.get());
        GLenum bufs[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, bufs);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    paper_program_->use();

    // Set uniforms
    // Model matrix (identity for now)
    float model[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    paper_program_->setUniformMatrix4fv("u_model", model);

    // View matrix (identity - camera at origin looking down -Z)
    float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    paper_program_->setUniformMatrix4fv("u_view", view);

    // Projection matrix (orthographic for paper)
    float aspect = (viewport_height_ > 0) ?
        static_cast<float>(viewport_width_) / static_cast<float>(viewport_height_) : 1.0f;
    float proj[16] = {
        1.0f/aspect, 0, 0, 0,
        0, 1.0f, 0, 0,
        0, 0, -1.0f, -1.0f,
        0, 0, 0, 1.0f
    };
    paper_program_->setUniformMatrix4fv("u_projection", proj);

    // Lighting uniforms
    paper_program_->setUniform("u_light_dir", light_.direction);
    paper_program_->setUniform("u_light_color", light_.color);
    paper_program_->setUniform("u_light_intensity", light_.intensity);
    paper_program_->setUniform("u_ambient", light_.ambient);
    paper_program_->setUniform("u_env_color", light_.env_color);
    paper_program_->setUniform("u_env_intensity", light_.env_intensity);

    // Material uniforms
    paper_program_->setUniform("u_paper_color", material_.color);
    paper_program_->setUniform("u_roughness", material_.roughness);
    paper_program_->setUniform("u_specular_strength", material_.specular_strength);
    paper_program_->setUniform("u_specular_roughness", material_.specular_roughness);
    paper_program_->setUniform("u_fiber_strength", material_.fiber_strength);
    paper_program_->setUniform("u_grain_scale", material_.grain_scale);
    paper_program_->setUniform("u_thickness", material_.thickness);

    // Bind textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fiber_normal_tex_->get());
    paper_program_->setUniform("u_fiber_normal", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, grain_tex_->get());
    paper_program_->setUniform("u_grain", 1);

    // Draw paper
    bindGeometry(paper_geom_);
    drawGeometry(paper_geom_);
}

void PaperRenderer::renderLightingPass() {
    // Lighting is combined in the geometry pass for Tier A (forward rendering)
    // Tier B/C could add a separate deferred lighting pass here
}

void PaperRenderer::renderInkPass() {
    // Ink simulation - Phase 7. Stub for now.
    if (!config_.enable_ink) return;
}

void PaperRenderer::renderCompositePass() {
    if (gbuffer_fbo_.valid() && gbuffer_color_.valid()) {
        // Composite G-buffer color to default framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_DEPTH_TEST);

        composite_program_->use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gbuffer_color_.get());
        composite_program_->setUniform("u_scene", 0);

        // Full-screen quad
        GLuint vao, vbo, ebo;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kQuadIndices), kQuadIndices, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ebo);

        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
    }
}

void PaperRenderer::renderFrame() {
    if (!initialized_) return;

    // Geometry pass (renders to G-buffer or default)
    renderGeometryPass();

    // Lighting pass (combined in forward for Tier A)
    renderLightingPass();

    // Ink pass
    renderInkPass();

    // Composite to screen
    renderCompositePass();
}

void PaperRenderer::endFrame() {
    // Present is handled by the Android framework (eglSwapBuffers via GLSurfaceView)
    // No action needed here
}
