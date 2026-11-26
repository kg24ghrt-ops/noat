#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace noat {

// A single positioned glyph instance produced by the shaper.
// All instances sharing a cluster_id must receive the same warp/rotation/offset
// so stacked consonants and marks stay attached.
struct GlyphInstance {
    uint32_t glyph_id = 0;
    float x = 0.0f;   // pen x before this glyph (font units at `size`)
    float y = 0.0f;   // pen y before this glyph
    float x_advance = 0.0f;
    float y_advance = 0.0f;
    float x_offset = 0.0f;  // glyph placement offset
    float y_offset = 0.0f;
    uint32_t cluster_id = 0; // dense grapheme-cluster id
};

// One shaped run: a contiguous span of text with one direction/script/language.
struct ShapedRun {
    std::string font;
    float size = 0.0f;
    uint32_t glyph_count = 0;
    uint32_t cluster_count = 0;
    std::vector<GlyphInstance> instances;
};

// Shaped-run interchange artifact (JSON). Identical format consumed by the
// headless executable and (later) the app shell so goldens cannot diverge.
std::string shaped_run_to_json(const ShapedRun& run);

// Shape UTF-8 text with HarfBuzz. Returns empty run on failure.
// `font_data` must outlive the returned run's use (face references the blob).
ShapedRun shape_text(const char* utf8, int32_t byte_length,
                     const unsigned char* font_data, size_t font_bytes,
                     float font_size_pt, const char* direction,
                     const char* script, const char* language);

bool load_file_bytes(const std::string& path, std::vector<unsigned char>& out);

} // namespace noat
