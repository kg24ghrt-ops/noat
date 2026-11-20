#include <jni.h>

#include <android/log.h>

#include "core/capabilities.h"
#include "rendering/renderer.h"

#include <memory>
#include <mutex>

#define TAG "PaperEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

// Singleton renderer instance accessed from JNI
namespace {
std::unique_ptr<PaperRenderer> g_renderer;
std::mutex g_renderer_mutex;
GPUCaps g_caps;
bool g_initialized = false;
} // namespace

extern "C" {

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperInit(JNIEnv* env, jobject /* thiz */) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    if (g_renderer) {
        LOGW("Paper engine already initialized");
        return;
    }

    // Capabilities are detected on the render thread after GLES context is current.
    // For now, initialize with default caps; paperInitWithCaps is called from
    // the GL thread with actual context queries.
    g_renderer = std::make_unique<PaperRenderer>();
    g_initialized = true;
    LOGI("Paper engine initialized (JNI)");
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperInitWithCaps(JNIEnv* env, jobject /* thiz */,
                                                  jint major, jint minor,
                                                  jint glsl_major, jint glsl_minor,
                                                  jstring renderer, jstring vendor,
                                                  jstring version, jstring glsl_version,
                                                  jboolean is_mali,
                                                  jint max_texture_size,
                                                  jint max_texture_image_units,
                                                  jint max_combined_texture_image_units,
                                                  jint max_vertex_attribs,
                                                  jint max_draw_buffers,
                                                  jboolean has_tessellation,
                                                  jboolean has_compute_shader,
                                                  jboolean has_astc,
                                                  jboolean has_etc2,
                                                  jboolean has_framebuffer_fetch) {
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    if (!g_renderer) {
        LOGE("Paper engine not initialized. Call paperInit first.");
        return;
    }

    // Build GPUCaps from Java-provided values (queried on the GL thread)
    GPUCaps caps;
    caps.major = static_cast<int>(major);
    caps.minor = static_cast<int>(minor);
    caps.glsl_major = static_cast<int>(glsl_major);
    caps.glsl_minor = static_cast<int>(glsl_minor);

    auto getString = [env](jstring jstr) -> std::string {
        if (!jstr) return "";
        const char* chars = env->GetStringUTFChars(jstr, nullptr);
        std::string result(chars ? chars : "");
        if (chars) env->ReleaseStringUTFChars(jstr, chars);
        return result;
    };

    caps.renderer = getString(renderer);
    caps.vendor = getString(vendor);
    caps.version = getString(version);
    caps.shading_language_version = getString(glsl_version);

    caps.is_mali = (is_mali == JNI_TRUE);
    caps.max_texture_size = max_texture_size;
    caps.max_texture_image_units = max_texture_image_units;
    caps.max_combined_texture_image_units = max_combined_texture_image_units;
    caps.max_vertex_attribs = max_vertex_attribs;
    caps.max_draw_buffers = max_draw_buffers;

    caps.has_tessellation = (has_tessellation == JNI_TRUE);
    caps.has_compute_shader = (has_compute_shader == JNI_TRUE);
    caps.has_astc = (has_astc == JNI_TRUE);
    caps.has_etc2 = (has_etc2 == JNI_TRUE);
    caps.has_framebuffer_fetch = (has_framebuffer_fetch == JNI_TRUE);

    // Set recommended tier
    if (caps.has_compute_shader && caps.minor >= 1) {
        caps.recommended_tier = GPUCaps::Tier::B;
    } else {
        caps.recommended_tier = GPUCaps::Tier::A;
    }

    g_caps = caps;

    LOGI("GPU caps: ES %d.%d, GLSL ES %d.%d, renderer=%s, mali=%d, tier=%d",
         caps.major, caps.minor, caps.glsl_major, caps.glsl_minor,
         caps.renderer.c_str(), caps.is_mali ? 1 : 0,
         static_cast<int>(caps.recommended_tier));

    if (!g_renderer) {
        LOGE("Paper engine not initialized. Call paperInit first.");
        return;
    }

    // Initialize the renderer (compiles shaders, creates geometry/textures)
    LOGI("Calling renderer->initialize()...");
    try {
        g_renderer->initialize(caps);
        LOGI("Paper renderer initialized successfully");
    } catch (const std::exception& e) {
        LOGE("Failed to initialize paper renderer: %s", e.what());
        throw;
    } catch (...) {
        LOGE("Failed to initialize paper renderer: unknown exception");
        throw;
    }
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperSetQuality(JNIEnv* env, jobject /* thiz */, jint quality) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    if (!g_renderer) return;

    RenderConfig config = {};
    switch (quality) {
        case 0: config.quality = QualityLevel::Low; break;
        case 1: config.quality = QualityLevel::Balanced; break;
        case 2: config.quality = QualityLevel::High; break;
        case 3: config.quality = QualityLevel::Ultra; break;
        default: config.quality = QualityLevel::Balanced; break;
    }
    // Tessellation is opt-in only (off by default per Arm Mali guidance)
    config.enable_tessellation = false;
    config.enable_compute_prepass = g_caps.has_compute_shader;
    config.enable_msaa = false;
    config.max_subdivision = 64;

    g_renderer->setConfig(config);
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperSetLight(JNIEnv* env, jobject /* thiz */,
                                              jfloat dir_x, jfloat dir_y, jfloat dir_z,
                                              jfloat color_r, jfloat color_g, jfloat color_b,
                                              jfloat intensity, jfloat ambient,
                                              jfloat env_r, jfloat env_g, jfloat env_b,
                                              jfloat env_intensity) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    if (!g_renderer) return;

    LightParams light;
    light.direction[0] = dir_x;
    light.direction[1] = dir_y;
    light.direction[2] = dir_z;
    light.color[0] = color_r;
    light.color[1] = color_g;
    light.color[2] = color_b;
    light.intensity = intensity;
    light.ambient = ambient;
    light.env_color[0] = env_r;
    light.env_color[1] = env_g;
    light.env_color[2] = env_b;
    light.env_intensity = env_intensity;

    g_renderer->setLight(light);
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperSetMaterial(JNIEnv* env, jobject /* thiz */,
                                                 jint paper_type,
                                                 jfloat r, jfloat g, jfloat b,
                                                 jfloat roughness,
                                                 jfloat specular_strength,
                                                 jfloat specular_roughness,
                                                 jfloat fiber_strength,
                                                 jfloat grain_scale,
                                                 jfloat thickness) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    if (!g_renderer) return;

    PaperMaterialParams mat = PaperMaterialPresets::get(
        static_cast<PaperType>(paper_type));

    // Override with JNI-provided values if they differ from defaults
    mat.color[0] = r;
    mat.color[1] = g;
    mat.color[2] = b;
    mat.roughness = roughness;
    mat.specular_strength = specular_strength;
    mat.specular_roughness = specular_roughness;
    mat.fiber_strength = fiber_strength;
    mat.grain_scale = grain_scale;
    mat.thickness = thickness;

    g_renderer->setMaterial(mat);
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperResize(JNIEnv* env, jobject /* thiz */,
                                            jint width, jint height) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    if (!g_renderer) return;
    g_renderer->resize(static_cast<GLsizei>(width), static_cast<GLsizei>(height));
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperBeginFrame(JNIEnv* env, jobject /* thiz */) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    if (!g_renderer) return;
    g_renderer->beginFrame();
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperRenderFrame(JNIEnv* env, jobject /* thiz */) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    if (!g_renderer) return;
    g_renderer->renderFrame();
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperEndFrame(JNIEnv* env, jobject /* thiz */) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    if (!g_renderer) return;
    g_renderer->endFrame();
}

JNIEXPORT jstring JNICALL
Java_com_example_noat_PaperLib_paperGetInfo(JNIEnv* env, jobject /* thiz */) {
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    if (!g_renderer) {
        return env->NewStringUTF("Paper engine not initialized");
    }
    const GPUCaps& caps = g_renderer->capabilities();
    std::string info = "Paper Engine | ";
    info += "ES " + std::to_string(caps.major) + "." + std::to_string(caps.minor);
    info += " | " + caps.renderer;
    info += " | Tier " + std::to_string(static_cast<int>(caps.recommended_tier));
    return env->NewStringUTF(info.c_str());
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperShutdown(JNIEnv* env, jobject /* thiz */) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    g_renderer.reset();
    g_initialized = false;
    LOGI("Paper engine shutdown");
}

} // extern "C"
