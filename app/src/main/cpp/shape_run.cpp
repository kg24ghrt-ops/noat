// shape_run.cpp - HarfBuzz complex-script shaping -> positioned glyph instances
// with grapheme-cluster boundaries. Deterministic; no wall-clock, no ML.

#include "shape_run.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef NOAT_HAVE_HARFBUZZ
#include <hb.h>
#endif

namespace noat {

bool load_file_bytes(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    auto size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    out.resize(size);
    if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()),
                               std::streamsize(size)).good()) {
        return false;
    }
    return true;
}

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

std::string shaped_run_to_json(const ShapedRun& run) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"font\": \"" << json_escape(run.font) << "\",\n";
    o << "  \"size\": " << run.size << ",\n";
    o << "  \"glyph_count\": " << run.glyph_count << ",\n";
    o << "  \"cluster_count\": " << run.cluster_count << ",\n";
    o << "  \"instances\": [\n";
    for (size_t i = 0; i < run.instances.size(); ++i) {
        const auto& g = run.instances[i];
        o << "    {\"gid\":" << g.glyph_id
          << ",\"x\":" << g.x << ",\"y\":" << g.y
          << ",\"xa\":" << g.x_advance << ",\"ya\":" << g.y_advance
          << ",\"xo\":" << g.x_offset << ",\"yo\":" << g.y_offset
          << ",\"cluster\":" << g.cluster_id << "}";
        if (i + 1 < run.instances.size()) o << ",";
        o << "\n";
    }
    o << "  ]\n";
    o << "}\n";
    return o.str();
}

ShapedRun shape_text(const char* utf8, int32_t byte_length,
                     const unsigned char* font_data, size_t font_bytes,
                     float font_size_pt, const char* direction,
                     const char* script, const char* language) {
    ShapedRun empty;
#ifndef NOAT_HAVE_HARFBUZZ
    return empty;
#else
    ShapedRun run;
    run.font = font_data ? "(blob)" : "(none)";
    run.size = font_size_pt;

    hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(font_data),
                                     static_cast<unsigned int>(font_bytes),
                                     HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    if (!blob) return empty;
    hb_face_t* face = hb_face_create(blob, 0);
    hb_blob_destroy(blob);
    if (!face) return empty;
    hb_font_t* font = hb_font_create(face);
    hb_face_destroy(face);
    if (!font) return empty;
    hb_font_set_scale(font, static_cast<unsigned>(font_size_pt * 64.0f),
                      static_cast<unsigned>(font_size_pt * 64.0f));

    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, utf8, byte_length, 0, byte_length);

    hb_direction_t dir = HB_DIRECTION_LTR;
    if (std::strcmp(direction, "rtl") == 0) dir = HB_DIRECTION_RTL;
    else if (std::strcmp(direction, "ttb") == 0) dir = HB_DIRECTION_TTB;
    else if (std::strcmp(direction, "btt") == 0) dir = HB_DIRECTION_BTT;
    hb_buffer_set_direction(buf, dir);

    hb_script_t scr = HB_SCRIPT_LATIN;
    if (std::strcmp(script, "Mymr") == 0) scr = HB_SCRIPT_MYANMAR;
    else if (std::strcmp(script, "latn") == 0) scr = HB_SCRIPT_LATIN;
    hb_buffer_set_script(buf, scr);

    hb_language_t lang = hb_language_from_string(language, -1);
    if (!lang) lang = hb_language_from_string("en", -1);
    hb_buffer_set_language(buf, lang);

    hb_shape(font, buf, nullptr, 0);

    uint32_t count = 0;
    const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &count);
    const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &count);
    if (!info || !pos || count == 0) {
        hb_buffer_destroy(buf);
        hb_font_destroy(font);
        return empty;
    }

    run.glyph_count = static_cast<uint32_t>(count);
    run.instances.resize(count);

    // Dense cluster ids: all glyphs sharing an hb cluster value get one id.
    uint32_t next_cluster = 0;
    // Sparse map from hb cluster value -> dense id. Cluster values are small
    // integers; use a vector sized to max+1.
    uint32_t max_cluster = 0;
    for (uint32_t i = 0; i < count; ++i)
        max_cluster = std::max(max_cluster, info[i].cluster);
    std::vector<uint32_t> cluster_map(max_cluster + 1, UINT32_MAX);

    float pen_x = 0.0f, pen_y = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t hb_cluster = info[i].cluster;
        if (cluster_map[hb_cluster] == UINT32_MAX)
            cluster_map[hb_cluster] = next_cluster++;
        GlyphInstance& g = run.instances[i];
        g.glyph_id = info[i].codepoint;
        g.x = pen_x;
        g.y = pen_y;
        g.x_advance = pos[i].x_advance / 64.0f;
        g.y_advance = pos[i].y_advance / 64.0f;
        g.x_offset = pos[i].x_offset / 64.0f;
        g.y_offset = pos[i].y_offset / 64.0f;
        g.cluster_id = cluster_map[hb_cluster];
        pen_x += g.x_advance;
        pen_y += g.y_advance;
    }

    run.cluster_count = next_cluster;
    hb_buffer_destroy(buf);
    hb_font_destroy(font);
    return run;
#endif
}

} // namespace noat
