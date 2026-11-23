/*
 * noat_jni.c - JNI bridge for noat paper renderer (C implementation)
 */

#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <pthread.h>

#include "noat_vulkan.h"
#include "noat_paper_geometry.h"

#define TAG "PaperLib"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static NoatRenderer *g_renderer = NULL;
static pthread_mutex_t g_renderer_mutex = PTHREAD_MUTEX_INITIALIZER;

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperInitWithSurface(JNIEnv *env, jobject thiz, jobject surface) {
    (void)env;
    (void)thiz;

    pthread_mutex_lock(&g_renderer_mutex);

    if (g_renderer) {
        LOGW("Paper engine already initialized");
        pthread_mutex_unlock(&g_renderer_mutex);
        return;
    }

    ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        LOGE("Failed to get ANativeWindow from Surface");
        pthread_mutex_unlock(&g_renderer_mutex);
        return;
    }
    LOGI("ANativeWindow acquired: %p", (void*)window);

    NoatRenderer *renderer = noat_renderer_create();
    if (!renderer || !noat_renderer_init(renderer, window)) {
        LOGE("Failed to initialize paper renderer");
        if (renderer) noat_renderer_destroy(renderer);
        ANativeWindow_release(window);
        pthread_mutex_unlock(&g_renderer_mutex);
        return;
    }

    ANativeWindow_release(window);
    g_renderer = renderer;
    LOGI("Paper engine initialized with surface");
    pthread_mutex_unlock(&g_renderer_mutex);
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperSetQuality(JNIEnv *env, jobject thiz, jint quality) {
    (void)env;
    (void)thiz;

    pthread_mutex_lock(&g_renderer_mutex);
    if (!g_renderer) {
        pthread_mutex_unlock(&g_renderer_mutex);
        return;
    }
    noat_renderer_set_quality(g_renderer, quality);
    pthread_mutex_unlock(&g_renderer_mutex);
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperSetLight(JNIEnv *env, jobject thiz,
                                              jfloat dir_x, jfloat dir_y, jfloat dir_z,
                                              jfloat color_r, jfloat color_g, jfloat color_b,
                                              jfloat intensity, jfloat ambient,
                                              jfloat env_r, jfloat env_g, jfloat env_b,
                                              jfloat env_intensity) {
    (void)env;
    (void)thiz;

    pthread_mutex_lock(&g_renderer_mutex);
    if (!g_renderer) {
        pthread_mutex_unlock(&g_renderer_mutex);
        return;
    }

    NoatLightParams light = {
        .direction = {dir_x, dir_y, dir_z},
        .color = {color_r, color_g, color_b},
        .intensity = intensity,
        .ambient = ambient,
        .env_color = {env_r, env_g, env_b},
        .env_intensity = env_intensity,
    };
    noat_renderer_set_light(g_renderer, &light);
    pthread_mutex_unlock(&g_renderer_mutex);
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperSetMaterial(JNIEnv *env, jobject thiz,
                                                  jint paper_type,
                                                  jfloat r, jfloat g, jfloat b,
                                                  jfloat roughness, jfloat specular_strength,
                                                  jfloat specular_roughness, jfloat fiber_strength,
                                                  jfloat grain_scale, jfloat thickness) {
    (void)env;
    (void)thiz;

    pthread_mutex_lock(&g_renderer_mutex);
    if (!g_renderer) {
        pthread_mutex_unlock(&g_renderer_mutex);
        return;
    }

    NoatMaterialParams mat = {
        .color = {r, g, b},
        .roughness = roughness,
        .specular_strength = specular_strength,
        .specular_roughness = specular_roughness,
        .fiber_strength = fiber_strength,
        .grain_scale = grain_scale,
        .thickness = thickness,
    };
    noat_renderer_set_material(g_renderer, &mat);
    pthread_mutex_unlock(&g_renderer_mutex);
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperResize(JNIEnv *env, jobject thiz, jint width, jint height) {
    (void)env;
    (void)thiz;

    pthread_mutex_lock(&g_renderer_mutex);
    if (!g_renderer) {
        pthread_mutex_unlock(&g_renderer_mutex);
        return;
    }
    noat_renderer_resize(g_renderer, width, height);
    pthread_mutex_unlock(&g_renderer_mutex);
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperSetSize(JNIEnv *env, jobject thiz, jstring size_id) {
    (void)env;
    (void)thiz;

    pthread_mutex_lock(&g_renderer_mutex);
    if (!g_renderer) {
        pthread_mutex_unlock(&g_renderer_mutex);
        return;
    }

    const char *id = (*env)->GetStringUTFChars(env, size_id, NULL);
    if (id) {
        noat_renderer_set_paper_size(g_renderer, id);
        (*env)->ReleaseStringUTFChars(env, size_id, id);
    }
    pthread_mutex_unlock(&g_renderer_mutex);
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperRenderFrame(JNIEnv *env, jobject thiz) {
    (void)env;
    (void)thiz;

    pthread_mutex_lock(&g_renderer_mutex);
    if (!g_renderer) {
        pthread_mutex_unlock(&g_renderer_mutex);
        return;
    }
    noat_renderer_render_frame(g_renderer);
    pthread_mutex_unlock(&g_renderer_mutex);
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperShutdown(JNIEnv* env, jobject /* thiz */) {
    (void)env;
    (void)thiz;

    pthread_mutex_lock(&g_renderer_mutex);
    if (g_renderer) {
        noat_renderer_destroy(g_renderer);
        g_renderer = NULL;
    }
    pthread_mutex_unlock(&g_renderer_mutex);
}

JNIEXPORT jstring JNICALL
Java_com_example_noat_PaperLib_paperGetInfo(JNIEnv* env, jobject /* thiz */) {
    (void)env;
    (void)thiz;

    pthread_mutex_lock(&g_renderer_mutex);
    if (!g_renderer) {
        pthread_mutex_unlock(&g_renderer_mutex);
        return (*env)->NewStringUTF(env, "Paper engine not initialized");
    }

    const NoatPaperSize *size = g_renderer->current_size;
    char info[128];
    snprintf(info, sizeof(info), "Paper engine: %s (%.0fx%.0f mm)",
             size ? size->name : "unknown",
             size ? size->width_mm : 0.0f,
             size ? size->height_mm : 0.0f);
    pthread_mutex_unlock(&g_renderer_mutex);
    return (*env)->NewStringUTF(env, info);
}
    pthread_mutex_unlock(&g_renderer_mutex);
}