#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace noat {

// Rasterized glyph mask at a given supersampling scale.
struct GlyphMask {
    uint32_t glyph_id = 0;
    int32_t width = 0;     // in supersampled pixels
    int32_t height = 0;
    int32_t stride = 0;    // row bytes
    std::vector<uint8_t> alpha; // 0..255 coverage, stride*height

    // Font-unit metrics (unscaled).
    int32_t advance_width = 0;
    int32_t left_side_bearing = 0;
    int32_t xMin = 0, yMin = 0, xMax = 0, yMax = 0;
};

// A baked SDF atlas entry: where the glyph lives in the atlas and its metrics.
struct AtlasGlyph {
    uint32_t glyph_id = 0;
    // Atlas rect (pixels in the final RGBA8 atlas).
    int32_t atlas_x = 0, atlas_y = 0, atlas_w = 0, atlas_h = 0;
    // Offset from glyph origin to atlas bitmap origin (pixels, final scale).
    int32_t offset_x = 0, offset_y = 0;
    // Metrics at final scale (pixels).
    float advance_x = 0.0f;
    float bearing_x = 0.0f;
    float xMin = 0.0f, yMin = 0.0f, xMax = 0.0f, yMax = 0.0f;
};

// Result of baking an SDF atlas for a set of glyph IDs at `ppem` pixels per em.
struct SdfAtlasResult {
    float ppem = 0.0f;
    float scale = 0.0f;            // final-scale pixels per font unit
    int32_t atlas_width = 0;
    int32_t atlas_height = 0;
    // RGBA8: R = signed distance (mapped 0..255), GBA unused/zero.
    std::vector<uint8_t> atlas_rgba8;
    std::vector<AtlasGlyph> glyphs;
};

// Load TrueType tables from a font blob and rasterize the requested glyph IDs
// into an SDF atlas. `font_units_per_em` overrides the head table value if > 0.
// Returns empty result on failure. No external dependencies (pure C++).
SdfAtlasResult bake_sdf_atlas(const unsigned char* font_data, size_t font_size,
                              const std::vector<uint32_t>& glyph_ids,
                              float ppem, int32_t supersample,
                              int32_t font_units_per_em_override = 0);

} // namespace noat
