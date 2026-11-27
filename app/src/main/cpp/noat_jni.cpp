// JNI bridge for Milestone 5 app shell.
// Connects PaperLib (Kotlin) to AppRenderer (C++ raw Vulkan).
// Parameters are kept in a host-side Params mirror and flushed to the
// VMA uniform buffer before each renderFrame.

#include <jni.h>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include "renderer.h"
#include "text_shaper.h"
#include "sdf_atlas.h"

#include <cstring>
#include <sstream>
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

// Application asset manager, set once from Java via paperSetAssetManager.
// Used to load bundled font assets for HarfBuzz shaping.
static AAssetManager* g_asset_manager{nullptr};

// Baked SDF atlas retained between prepare + upload calls.
static std::vector<uint8_t> g_atlasData;
static int g_atlasWidth{0};
static int g_atlasHeight{0};

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
    } else if (std::strcmp(sizeId, "B5") == 0) {
        p.paper_size_mm_x = 176.0f;
        p.paper_size_mm_y = 250.0f;
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
Java_com_example_noat_PaperLib_paperInitWithSurface(JNIEnv* env, jobject, jobject surface) {
    auto& r = renderer();
    if (!r.init()) {
        return;
    }
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        return;
    }
    if (!r.createSurface(window)) {
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
        return;
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

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperSetAssetManager(JNIEnv* env, jobject, jobject assetManager) {
    g_asset_manager = AAssetManager_fromJava(env, assetManager);
}

JNIEXPORT jstring JNICALL
Java_com_example_noat_PaperLib_paperShapeText(JNIEnv* env, jobject,
                                              jstring fontName, jstring text,
                                              jfloat fontSize,
                                              jstring direction, jstring script,
                                              jstring language) {
    auto get_utf = [&](jstring s) -> std::string {
        if (!s) return {};
        const char* c = env->GetStringUTFChars(s, nullptr);
        std::string out(c ? c : "");
        if (c) env->ReleaseStringUTFChars(s, c);
        return out;
    };
    std::string font_name = get_utf(fontName);
    std::string utf8 = get_utf(text);
    std::string direction = get_utf(direction);
    if (direction.empty()) direction = "ltr";
    std::string script = get_utf(script);
    if (script.empty()) script = "latn";
    std::string language = get_utf(language);
    if (language.empty()) language = "en";

    ShapedRun run = shape_text_from_asset(g_asset_manager, font_name.c_str(),
                                          utf8.c_str(), static_cast<int32_t>(utf8.size()),
                                          font_size, direction.c_str(),
                                          script.c_str(), language.c_str());
    std::string json = shaped_run_to_json(run);
    return env->NewStringUTF(json.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_example_noat_PaperLib_paperTextPrepareAtlas(JNIEnv* env, jobject,
                                                     jintArray glyphIds,
                                                     jfloat ppem,
                                                     jint supersample,
                                                     jstring fontName) {
    auto get_utf = [&](jstring s) -> std::string {
        if (!s) return {};
        const char* c = env->GetStringUTFChars(s, nullptr);
        std::string out(c ? c : "");
        if (c) env->ReleaseStringUTFChars(s, c);
        return out;
    };
    std::string font_name = get_utf(fontName);
    if (font_name.empty()) font_name = "fonts/NotoSansMyanmar-VF.ttf";

    AAssetManager* mgr = g_asset_manager;
    std::vector<unsigned char> font_data;
    if (mgr) {
        AAsset* asset = AAssetManager_open(mgr, font_name.c_str(),
                                           AASSET_MODE_BUFFER);
        if (asset) {
            off_t len = AAsset_getLength(asset);
            const void* buf = AAsset_getBuffer(asset);
            if (buf && len > 0) {
                font_data.assign(static_cast<const unsigned char*>(buf),
                                 static_cast<const unsigned char*>(buf) +
                                     (size_t)len);
            }
            AAsset_close(asset);
        }
    }

    jint* ids = env->GetIntArrayElements(glyphIds, nullptr);
    jsize n = env->GetArrayLength(glyphIds);
    std::vector<uint32_t> glyph_ids(n);
    for (jsize i = 0; i < n; ++i) glyph_ids[i] = (uint32_t)ids[i];
    env->ReleaseIntArrayElements(glyphIds, ids, JNI_ABORT);

    g_atlasData.clear();
    g_atlasWidth = 0;
    g_atlasHeight = 0;

    SdfAtlasResult res = bake_sdf_atlas(font_data.empty() ? nullptr
                                                          : font_data.data(),
                                        font_data.size(), glyph_ids,
                                        (float)ppem, (int)supersample);
    if (!res.glyphs.empty()) {
        g_atlasData = std::move(res.atlas_rgba8);
        g_atlasWidth = res.atlas_width;
        g_atlasHeight = res.atlas_height;
    }

    // Meta JSON describing the baked atlas (per-glyph rects for positioning).
    std::ostringstream meta;
    meta << "{\"width\":" << g_atlasWidth << ",\"height\":" << g_atlasHeight
         << ",\"ppem\":" << ppem << ",\"scale\":" << res.scale
         << ",\"glyphs\":[";
    for (size_t i = 0; i < res.glyphs.size(); ++i) {
        const auto& g = res.glyphs[i];
        meta << "{\"gid\":" << g.glyph_id << ",\"x\":" << g.atlas_x
             << ",\"y\":" << g.atlas_y << ",\"w\":" << g.atlas_w << ",\"h\":"
             << g.atlas_h << ",\"advance_x\":" << g.advance_x
             << ",\"bearing_x\":" << g.bearing_x << "}";
        if (i + 1 < res.glyphs.size()) meta << ",";
    }
    meta << "]}";
    return env->NewStringUTF(meta.str().c_str());
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperTextUploadInstances(JNIEnv* env, jobject,
                                                        jfloatArray instances,
                                                        jint count) {
    auto& r = renderer();
    jsize n = env->GetArrayLength(instances);
    jfloat* data = env->GetFloatArrayElements(instances, nullptr);
    std::vector<AppRenderer::GlyphInstance> gi((size_t)count);
    for (jsize i = 0; i < count && i * 12 < n; ++i) {
        const jfloat* f = &data[i * 12];
        gi[i].x = f[0]; gi[i].y = f[1]; gi[i].w = f[2]; gi[i].h = f[3];
        gi[i].ax = f[4]; gi[i].ay = f[5]; gi[i].aw = f[6]; gi[i].ah = f[7];
        gi[i].glyph_id = f[8]; gi[i].cluster_id = f[9];
        gi[i].pad0 = f[10]; gi[i].pad1 = f[11];
    }
    env->ReleaseFloatArrayElements(instances, data, JNI_ABORT);
    r.textUploadInstances(gi.data(), (uint32_t)count);
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperTextClearInstances(JNIEnv*, jobject) {
    auto& r = renderer();
    r.textClearInstances();
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperTextSetAtlas(JNIEnv*, jobject) {
    auto& r = renderer();
    if (!g_atlasData.empty() && g_atlasWidth > 0 && g_atlasHeight > 0) {
        r.textSetAtlas(g_atlasData.data(), g_atlasWidth, g_atlasHeight);
    }
}

// ---- Text document editing ----

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperTextInsert(JNIEnv* env, jobject, jstring text) {
    auto& r = renderer();
    if (!text) return;
    const char* c = env->GetStringUTFChars(text, nullptr);
    if (c) {
        r.textInsert(c, static_cast<uint32_t>(strlen(c)));
        env->ReleaseStringUTFChars(text, c);
    }
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperTextBackspace(JNIEnv*, jobject) {
    renderer().textBackspace();
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperTextEnter(JNIEnv*, jobject) {
    renderer().textEnter();
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperTextMoveLeft(JNIEnv*, jobject) {
    renderer().textMoveLeft();
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperTextMoveRight(JNIEnv*, jobject) {
    renderer().textMoveRight();
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperTextMoveUp(JNIEnv*, jobject) {
    renderer().textMoveUp();
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperTextMoveDown(JNIEnv*, jobject) {
    renderer().textMoveDown();
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperTextMoveToLineStart(JNIEnv*, jobject) {
    renderer().textMoveToLineStart();
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperTextMoveToLineEnd(JNIEnv*, jobject) {
    renderer().textMoveToLineEnd();
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperTextSetCursor(JNIEnv*, jobject, jint cluster_index) {
    renderer().textSetCursor(static_cast<uint32_t>(cluster_index));
}

JNIEXPORT jfloat JNICALL
Java_com_example_noat_PaperLib_paperTextGetCursorX(JNIEnv*, jobject) {
    return renderer().textCursorX();
}

JNIEXPORT jfloat JNICALL
Java_com_example_noat_PaperLib_paperTextGetCursorY(JNIEnv*, jobject) {
    return renderer().textCursorY();
}

JNIEXPORT jint JNICALL
Java_com_example_noat_PaperLib_paperTextGetCursorLine(JNIEnv*, jobject) {
    return static_cast<jint>(renderer().textCursorLine());
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperTextSetComposing(JNIEnv* env, jobject, jstring text) {
    auto& r = renderer();
    if (!text) {
        r.textClearComposing();
        return;
    }
    const char* c = env->GetStringUTFChars(text, nullptr);
    if (c) {
        r.textSetComposing(c, static_cast<uint32_t>(strlen(c)));
        env->ReleaseStringUTFChars(text, c);
    }
}

JNIEXPORT void JNICALL
Java_com_example_noat_PaperLib_paperTextClearComposing(JNIEnv*, jobject) {
    renderer().textClearComposing();
}

} // extern "C"
