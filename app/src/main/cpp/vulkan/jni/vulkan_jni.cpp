#include <jni.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>

#include "renderer.h"
#include "material/material.h"

#include <memory>
#include <mutex>

#define TAG "PaperEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

namespace {
std::unique_ptr<PaperRenderer> g_renderer;
std::mutex g_renderer_mutex;
} // namespace

extern "C" {

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperInitWithSurface(JNIEnv* env, jobject /* thiz */, jobject surface) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    if (g_renderer) {
        LOGW("Paper engine already initialized");
        return;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        LOGE("Failed to get ANativeWindow from Surface");
        return;
    }

    g_renderer = std::make_unique<PaperRenderer>();
    if (!g_renderer->init(window)) {
        LOGE("Failed to initialize paper renderer");
        g_renderer.reset();
        ANativeWindow_release(window);
        return;
    }

    ANativeWindow_release(window);
    LOGI("Paper engine initialized with surface");
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperSetQuality(JNIEnv* env, jobject /* thiz */, jint quality) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    if (!g_renderer) return;

    QualityLevel q = QualityLevel::Balanced;
    switch (quality) {
        case 0: q = QualityLevel::Low; break;
        case 1: q = QualityLevel::Balanced; break;
        case 2: q = QualityLevel::High; break;
        case 3: q = QualityLevel::Ultra; break;
        default: q = QualityLevel::Balanced; break;
    }
    g_renderer->setConfig(q);
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
    light.envColor[0] = env_r;
    light.envColor[1] = env_g;
    light.envColor[2] = env_b;
    light.envIntensity = env_intensity;

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

    mat.color[0] = r;
    mat.color[1] = g;
    mat.color[2] = b;
    mat.roughness = roughness;
    mat.specularStrength = specular_strength;
    mat.specularRoughness = specular_roughness;
    mat.fiberStrength = fiber_strength;
    mat.grainScale = grain_scale;
    mat.thickness = thickness;

    g_renderer->setMaterial(mat);
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperResize(JNIEnv* env, jobject /* thiz */,
                                            jint width, jint height) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    if (!g_renderer) return;
    g_renderer->resize(static_cast<int>(width), static_cast<int>(height));
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperRenderFrame(JNIEnv* env, jobject /* thiz */) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    if (!g_renderer) return;
    g_renderer->renderFrame();
}

JNIEXPORT jstring JNICALL
Java_com_example_noat_PaperLib_paperGetInfo(JNIEnv* env, jobject /* thiz */) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    if (!g_renderer) {
        return env->NewStringUTF("Paper engine not initialized");
    }
    return env->NewStringUTF("Paper Engine (Vulkan) | Mali TBR optimized");
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperShutdown(JNIEnv* env, jobject /* thiz */) {
    (void)env;
    std::lock_guard<std::mutex> lock(g_renderer_mutex);
    g_renderer.reset();
    LOGI("Paper engine shutdown");
}

} // extern "C"
