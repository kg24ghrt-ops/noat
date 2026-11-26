// JNI bridge for Milestone 5 app shell.
// Connects PaperLib (Kotlin) to AppRenderer (C++ raw Vulkan).
// Parameters are kept in a host-side Params mirror and flushed to the
// VMA uniform buffer before each renderFrame.

#include <jni.h>

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include "renderer.h"

#include <cstring>
#include <vector>

namespace {

// Host-side mirror of the GLSL 'Params' UBO (std140). Field order and types
// must match shaders/paper_wear.frag and native/paper-headless/main.cpp exactly.
struct alignas(16) ParamsMirror {
    float paper_size_mm_x = 215.9f;
    float paper_size_mm_y = 279.4f;
    float warmth = 0.45f;
    float fiber = 0.90f;
    float formation = 0.75f;
    float speckle = 0.35f;
    float tooth = 0.85f;
    float aniso = 0.62f;
    float rule_style = 0.0f;
    float rule_gap_mm = 7.1f;
    float rule_top_mm = 25.4f;
    float rule_weight_mm = 0.22f;
    float rule_ink_r = 0.62f;
    float rule_ink_g = 0.76f;
    float rule_ink_b = 0.85f;
    float margin_enabled = 1.0f;
    float margin_x_mm = 31.75f;
    float margin_ink_r = 0.85f;
    float margin_ink_g = 0.45f;
    float margin_ink_b = 0.49f;
    float print_fade = 0.38f;
    float light_az_deg = 142.0f;
    float light_el_deg = 27.0f;
    float light_intensity = 1.05f;
    float light_temp_k = 3450.0f;
    float fill_intensity = 0.45f;
    float sheen = 0.40f;
    float exposure = 1.02f;
    float grain = 0.22f;
    float vignette = 0.38f;
    float content_seed = 4321.0f;
    float graphite_amount = 0.70f;
    float graphite_darkness = 0.85f;
    float graphite_smudge = 0.0f;
    float graphite_eraser = 0.0f;
    float pen_amount = 0.55f;
    float pen_color_r = 0.10f;
    float pen_color_g = 0.16f;
    float pen_color_b = 0.24f;
    float pen_feather = 0.50f;
    float teacher_amount = 0.45f;
    float teacher_color_r = 0.80f;
    float teacher_color_g = 0.30f;
    float teacher_color_b = 0.34f;
    float teacher_marks = 1.0f;
    float smudge_amount = 0.25f;
    float eraser_amount = 0.15f;
    float quality = 1.0f;
    float curl_amount = 0.0f;
    float curl_freq = 0.02f;
    float corner_lift_amount = 0.0f;
    float crease_amount = 0.0f;
    float crease_x = 107.95f;
    float crease_y = 139.7f;
    float crease_angle = 0.0f;
    float hole_style = 0.0f;
    float edge_style = 0.0f;
    float coffee_amount = 0.0f;
    float water_amount = 0.0f;
    float foxing_amount = 0.0f;
    float fingerprint_amount = 0.0f;
    float pencil_smudge_amount = 0.0f;
    float eraser_abrasion_amount = 0.0f;
    float edge_aging_amount = 0.0f;
    float translucency_amount = 0.0f;
};
static_assert(sizeof(ParamsMirror) == 272, "ParamsMirror must be 272 bytes");

noat::AppRenderer& renderer() {
    static noat::AppRenderer instance;
    return instance;
}

ParamsMirror& params() {
    static ParamsMirror instance;
    return instance;
}

// Map a PaperSizes id to rule style / gap (college ruled by default).
void applySizeId(const char* sizeId, ParamsMirror& p) {
    p.rule_style = 0.0f;  // college
    p.rule_gap_mm = 7.1f;
    p.rule_top_mm = 25.4f;
    if (std::strcmp(sizeId, "A3") == 0) {
        p.paper_size_mm_x = 297.0f;
        p.paper_size_mm_y = 420.0f;
    } else if (std::strcmp(sizeId, "A4") == 0) {
        p.paper_size_mm_x = 210.0f;
        p.paper_size_mm_y = 297.0f;
    } else if (std::strcmp(sizeId, "A5") == 0) {
        p.paper_size_mm_x = 148.0f;
        p.paper_size_mm_y = 210.0f;
    } else if (std::strcmp(sizeId, "A6") == 0) {
        p.paper_size_mm_x = 105.0f;
        p.paper_size_mm_y = 148.0f;
    } else if (std::strcmp(sizeId, "LETTER") == 0) {
        p.paper_size_mm_x = 215.9f;
        p.paper_size_mm_y = 279.4f;
    } else if (std::strcmp(sizeId, "LEGAL") == 0) {
        p.paper_size_mm_x = 215.9f;
        p.paper_size_mm_y = 355.6f;
    }
}


// Flush host params to the VMA uniform buffer.
// (Handled inline in paperRenderFrame via renderer().updateParams().)

} // namespace

extern "C" {

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperInitWithSurface(JNIEnv*, jobject, jobject surface) {
    auto& r = renderer();
    if (!r.init()) {
        __android_log_print(ANDROID_LOG_ERROR, "noat", "AppRenderer::init failed");
        return;
    }
    ANativeWindow* window = ANativeWindow_fromSurface(nullptr, surface);
    if (window == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, "noat", "ANativeWindow_fromSurface failed");
        return;
    }
    if (!r.createSurface(window)) {
        __android_log_print(ANDROID_LOG_ERROR, "noat", "createSurface failed");
        ANativeWindow_release(window);
        return;
    }
    ANativeWindow_release(window);
    // Flush initial params to UBO happens on first renderFrame.
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperSetQuality(JNIEnv*, jobject, jint quality) {
    auto& p = params();
    p.quality = static_cast<float>(quality);
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperSetLight(JNIEnv*, jobject,
                                             jfloat dirX, jfloat dirY, jfloat dirZ,
                                             jfloat colorR, jfloat colorG, jfloat colorB,
                                             jfloat intensity, jfloat ambient,
                                             jfloat envR, jfloat envG, jfloat envB, jfloat envIntensity) {
    auto& p = params();
    // Convert light direction to azimuth/elevation.
    p.light_intensity = intensity;
    p.fill_intensity = ambient;
    p.light_temp_k = 3450.0f;  // warm desk lamp
    (void)dirX; (void)dirY; (void)dirZ;
    (void)colorR; (void)colorG; (void)colorB;
    (void)envR; (void)envG; (void)envB; (void)envIntensity;
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperSetMaterial(JNIEnv*, jobject, jint paperType,
                                                jfloat r, jfloat g, jfloat b,
                                                jfloat roughness,
                                                jfloat specularStrength, jfloat specularRoughness,
                                                jfloat fiberStrength, jfloat grainScale, jfloat thickness) {
    auto& p = params();
    p.warmth = 0.45f;
    p.fiber = fiberStrength;
    p.tooth = grainScale;
    p.aniso = 0.62f;
    p.graphite_darkness = 0.85f;
    p.sheen = specularStrength;
    (void)paperType; (void)r; (void)g; (void)b;
    (void)roughness; (void)specularRoughness; (void)thickness;
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperSetSize(JNIEnv* env, jobject, jstring sizeId) {
    auto& p = params();
    const char* sz = env->GetStringUTFChars(sizeId, nullptr);
    if (sz != nullptr) {
        applySizeId(sz, p);
        env->ReleaseStringUTFChars(sizeId, sz);
    }
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperResize(JNIEnv*, jobject, jint width, jint height) {
    auto& r = renderer();
    r.resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperRenderFrame(JNIEnv*, jobject) {
    auto& r = renderer();
    auto& p = params();
    r.updateParams(&p, sizeof(p));
    if (!r.renderFrame()) {
        __android_log_print(ANDROID_LOG_ERROR, "noat", "renderFrame failed");
    }
}

JNIEXPORT jstring JNICALL
Java_com_example_noat_PaperLib_paperGetInfo(JNIEnv* env, jobject) {
    auto& r = renderer();
    return env->NewStringUTF(r.info());
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperShutdown(JNIEnv*, jobject) {
    auto& r = renderer();
    r.shutdown();
}

} // extern "C"
