// text_shaper.cpp - app-side HarfBuzz shaping using bundled font assets.
// Mirrors native/paper-headless/shape_run.cpp but reads the font from the
// APK assets via AAssetManager. Output JSON matches the headless --shape
// artifact so headless CI goldens cannot diverge from the app shell.

#include "text_shaper.h"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include <cstring>
#include <vector>

#ifdef NOAT_HAVE_HARFBUZZ
#include <hb.h>
#endif

namespace noat {

static bool read_asset_bytes(AAssetManager* mgr, const char* name,
                             std::vector<unsigned char>& out) {
    if (!mgr || !name) return false;
    AAsset* asset = AAssetManager_open(mgr, name, AASSET_MODE_BUFFER);
    if (!asset) return false;
    off_t len = AAsset_getLength(asset);
    if (len <= 0) {
        AAsset_close(asset);
        return false;
    }
    const void* buf = AAsset_getBuffer(asset);
    if (!buf) {
        AAsset_close(asset);
        return false;
    }
    out.assign(static_cast<const unsigned char*>(buf),
               static_cast<const unsigned char*>(buf) + static_cast<size_t>(len));
    AAsset_close(asset);
    return true;
}

ShapedRun shape_text_from_asset(void* asset_manager, const char* font_name,
                                const char* utf8, int32_t byte_length,
                                float font_size_pt, const char* direction,
                                const char* script, const char* language) {
    ShapedRun empty;
#ifndef NOAT_HAVE_HARFBUZZ
    return empty;
#else
    AAssetManager* mgr = static_cast<AAssetManager*>(asset_manager);
    std::vector<unsigned char> font_data;
    if (!read_asset_bytes(mgr, font_name, font_data)) {
        return empty;
    }
    return shape_text(utf8, byte_length, font_data.data(), font_data.size(),
                      font_size_pt, direction, script, language);
#endif
}

} // namespace noat
