#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "shape_run.h"

namespace noat {

// Shape UTF-8 text using a font bundled in the app assets.
// `asset_manager` is the Android AAssetManager* (passed as void* to keep this
// header free of Android NDK includes). Produces a ShapedRun whose JSON is
// byte-identical to the headless paper-headless --shape output for the same
// font/text/params, so headless CI goldens match the app shell pixel-for-pixel.
ShapedRun shape_text_from_asset(void* asset_manager,
                                const char* font_name,
                                const char* utf8, int32_t byte_length,
                                float font_size_pt,
                                const char* direction,
                                const char* script,
                                const char* language);

} // namespace noat
