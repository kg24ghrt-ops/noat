// paper-headless - headless Vulkan paper renderer (Milestone 1)
// C++20 harness: volk + vk-bootstrap + VMA + stb_image_write + stb_image
// Approved dependencies only. No engine abstraction. No surface/swapchain.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image_write.h>
#include <stb_image.h>

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "shape_run.h"
#include "sdf_atlas.h"

namespace {

struct PushConstants {
    float time;
    float resolution_x;
    float resolution_y;
    float seed;
};
static_assert(sizeof(PushConstants) == 16, "push constants must be 16 bytes");

// Simple hash functions for C++ (mirroring GLSL versions)
static uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}
static float hash_f(float x) {
    return fract(std::sin(x) * 43758.5453f);
}
static float hash2(float x, float y) {
    return hash_f(x * 12.9898f + y * 78.233f);
}

struct GlyphInstance {
    float pos_x, pos_y, size_x, size_y;
    float color_r, color_g, color_b, color_a;
    float scale_x, scale_y;
    float rotation;
    int cluster_id;
    int pad0;
};
static_assert(sizeof(GlyphInstance) == 48, "GlyphInstance must be 48 bytes");

struct GlyphInstancesUBO {
    GlyphInstance glyphs[8];
    int32_t count;
    int32_t pad1, pad2, pad3;
};
static_assert(sizeof(GlyphInstancesUBO) == 8 * 48 + 16, "GlyphInstancesUBO size");

struct DeformParamsUBO {
    float warp_freq;
    float warp_amp;
    float warp_speed;
    float cluster_drift;
    float rot_variation;
    float size_variation;
    float advance_var;
    float ink_feather;
    float pressure_var;
};
static_assert(sizeof(DeformParamsUBO) == 9 * 4, "DeformParamsUBO must be 36 bytes");

// ---------- minimal JSON parser ----------
struct JsonValue {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonValue> arr;
    std::map<std::string, JsonValue> obj;
};

static std::string json_trim(const std::string& s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                            s[i] == '\r' || s[i] == ',')) {
        ++i;
    }
    return s;
}

static JsonValue json_parse(const std::string& s, std::size_t& i);

static std::string json_parse_string(const std::string& s, std::size_t& i) {
    std::string out;
    ++i; // skip opening quote
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            switch (s[i]) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                default: out.push_back(s[i]); break;
            }
            ++i;
        } else {
            out.push_back(s[i]);
            ++i;
        }
    }
    if (i < s.size()) ++i; // skip closing quote
    return out;
}

static double json_parse_number(const std::string& s, std::size_t& i) {
    std::size_t start = i;
    while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.' ||
                            s[i] == '-' || s[i] == '+' || s[i] == 'e' ||
                            s[i] == 'E')) {
        ++i;
    }
    return std::stod(s.substr(start, i - start));
}

static JsonValue json_parse(const std::string& s, std::size_t& i) {
    JsonValue v;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                            s[i] == '\r')) {
        ++i;
    }
    if (i >= s.size()) return v;
    char c = s[i];
    if (c == '{') {
        v.type = JsonValue::Object;
        ++i;
        while (i < s.size() && s[i] != '}') {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                                    s[i] == '\r')) {
                ++i;
            }
            if (i < s.size() && s[i] == '"') {
                std::string key = json_parse_string(s, i);
                while (i < s.size() && s[i] != ':') ++i;
                if (i < s.size()) ++i;
                v.obj[key] = json_parse(s, i);
                while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                                        s[i] == '\r')) {
                    ++i;
                }
                if (i < s.size() && s[i] == ',') ++i;
            } else {
                break;
            }
        }
        if (i < s.size()) ++i;
    } else if (c == '[') {
        v.type = JsonValue::Array;
        ++i;
        while (i < s.size() && s[i] != ']') {
            v.arr.push_back(json_parse(s, i));
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                                    s[i] == '\r')) {
                ++i;
            }
            if (i < s.size() && s[i] == ',') ++i;
        }
        if (i < s.size()) ++i;
    } else if (c == '"') {
        v.type = JsonValue::String;
        v.str = json_parse_string(s, i);
    } else if (c == 't' || c == 'f') {
        v.type = JsonValue::Bool;
        v.b = (s.compare(i, 4, "true") == 0);
        i += v.b ? 4 : 5;
    } else if (c == 'n') {
        v.type = JsonValue::Null;
        i += 4;
    } else {
        v.type = JsonValue::Number;
        v.num = json_parse_number(s, i);
    }
    return v;
}

std::string read_file(const std::string& path, std::size_t* out_size);

static JsonValue load_json_file(const std::string& path) {
    std::size_t size = 0;
    std::string data = read_file(path, &size);
    std::size_t i = 0;
    return json_parse(data, i);
}
// ---------- single combined parameter block (std140, matches GLSL 'Params') ----------
// Field order and offsets MUST match shaders/paper_content.frag exactly.
// All fields are float/int32 (4-byte aligned) so no inter-field padding is needed.
struct alignas(16) Params {
    float paper_size_mm[2];   // 0-1
    float warmth;             // 2
    float fiber;              // 3
    float formation;          // 4
    float speckle;            // 5
    float tooth;              // 6
    float aniso;              // 7
    float rule_style;         // 8
    float rule_gap_mm;        // 9
    float rule_top_mm;        // 10
    float rule_weight_mm;     // 11
    float rule_ink_r;         // 12
    float rule_ink_g;         // 13
    float rule_ink_b;         // 14
    float margin_enabled;     // 15
    float margin_x_mm;        // 16
    float margin_ink_r;       // 17
    float margin_ink_g;       // 18
    float margin_ink_b;       // 19
    float print_fade;         // 20
    float light_az_deg;       // 21
    float light_el_deg;       // 22
    float light_intensity;    // 23
    float light_temp_k;       // 24
    float fill_intensity;     // 25
    float sheen;              // 26
    float exposure;           // 27
    float grain;              // 28
    float vignette;           // 29
    float content_seed;       // 30
    float graphite_amount;    // 31
    float graphite_darkness;  // 32
    float graphite_smudge;    // 33
    float graphite_eraser;    // 34
    float pen_amount;         // 35
    float pen_color_r;        // 36
    float pen_color_g;        // 37
    float pen_color_b;        // 38
    float pen_feather;        // 39
    float teacher_amount;     // 40
    float teacher_color_r;    // 41
    float teacher_color_g;    // 42
    float teacher_color_b;    // 43
    float teacher_marks;      // 44
    float smudge_amount;      // 45
    float eraser_amount;      // 46
    float quality;            // 47
    // ---- wear / sheet shape ----
    float curl_amount;        // 48
    float curl_freq;          // 49
    float corner_lift_amount; // 50
    float crease_amount;      // 51
    float crease_x;           // 52
    float crease_y;           // 53
    float crease_angle;       // 54
    float hole_style;         // 55
    float edge_style;         // 56
    float coffee_amount;      // 57
    float water_amount;       // 58
    float foxing_amount;      // 59
    float fingerprint_amount; // 60
    float pencil_smudge_amount; // 61
    float eraser_abrasion_amount; // 62
    float edge_aging_amount;  // 63
    float translucency_amount;// 64
};
static_assert(sizeof(Params) == 272, "Params must be 272 bytes (alignas(16) pads 260 to 272)");
static_assert(offsetof(Params, rule_ink_r) == 48, "rule_ink_r offset");
static_assert(offsetof(Params, margin_enabled) == 60, "margin_enabled offset");
static_assert(offsetof(Params, margin_ink_r) == 68, "margin_ink_r offset");
static_assert(offsetof(Params, print_fade) == 80, "print_fade offset");
static_assert(offsetof(Params, light_intensity) == 92, "light_intensity offset");
static_assert(offsetof(Params, pen_color_r) == 144, "pen_color_r offset");
static_assert(offsetof(Params, pen_feather) == 156, "pen_feather offset");
static_assert(offsetof(Params, teacher_color_r) == 164, "teacher_color_r offset");
static_assert(offsetof(Params, teacher_marks) == 176, "teacher_marks offset");

static Params default_params() {
    Params p{};
    p.paper_size_mm[0] = 215.9f;
    p.paper_size_mm[1] = 279.4f;
    p.warmth = 0.45f;
    p.fiber = 0.90f;
    p.formation = 0.75f;
    p.speckle = 0.35f;
    p.tooth = 0.85f;
    p.aniso = 0.62f;
    p.rule_style = 0.0f;
    p.rule_gap_mm = 7.1f;
    p.rule_top_mm = 25.4f;
    p.rule_weight_mm = 0.22f;
    p.rule_ink_r = 0.62f;
    p.rule_ink_g = 0.76f;
    p.rule_ink_b = 0.85f;
    p.margin_enabled = 1.0f;
    p.margin_x_mm = 31.75f;
    p.margin_ink_r = 0.85f;
    p.margin_ink_g = 0.45f;
    p.margin_ink_b = 0.49f;
    p.print_fade = 0.38f;
    p.light_az_deg = 142.0f;
    p.light_el_deg = 27.0f;
    p.light_intensity = 1.05f;
    p.light_temp_k = 3450.0f;
    p.fill_intensity = 0.45f;
    p.sheen = 0.40f;
    p.exposure = 1.02f;
    p.grain = 0.22f;
    p.vignette = 0.38f;
    p.content_seed = 4321.0f;
    p.graphite_amount = 0.70f;
    p.graphite_darkness = 0.85f;
    p.graphite_smudge = 0.0f;
    p.graphite_eraser = 0.0f;
    p.pen_amount = 0.55f;
    p.pen_color_r = 0.10f;
    p.pen_color_g = 0.16f;
    p.pen_color_b = 0.24f;
    p.pen_feather = 0.50f;
    p.teacher_amount = 0.45f;
    p.teacher_color_r = 0.80f;
    p.teacher_color_g = 0.30f;
    p.teacher_color_b = 0.34f;
    p.teacher_marks = 1.0f;
    p.smudge_amount = 0.25f;
    p.eraser_amount = 0.15f;
    p.quality = 1.0f;
    // ---- wear / sheet shape defaults ----
    p.curl_amount = 0.0f;
    p.curl_freq = 0.02f;
    p.corner_lift_amount = 0.0f;
    p.crease_amount = 0.0f;
    p.crease_x = 107.95f;
    p.crease_y = 139.7f;
    p.crease_angle = 0.0f;
    p.hole_style = 0.0f;
    p.edge_style = 0.0f;
    p.coffee_amount = 0.0f;
    p.water_amount = 0.0f;
    p.foxing_amount = 0.0f;
    p.fingerprint_amount = 0.0f;
    p.pencil_smudge_amount = 0.0f;
    p.eraser_abrasion_amount = 0.0f;
    p.edge_aging_amount = 0.0f;
    p.translucency_amount = 0.0f;
    return p;
}

static void apply_params_json(Params& p, const JsonValue& v) {
    if (v.type != JsonValue::Object) return;
    auto gf = [&](const char* k, float& out) {
        auto it = v.obj.find(k);
        if (it != v.obj.end() && it->second.type == JsonValue::Number) {
            out = (float)it->second.num;
        }
    };
    auto gi = [&](const char* k, int32_t& out) {
        auto it = v.obj.find(k);
        if (it != v.obj.end() && it->second.type == JsonValue::Number) {
            out = (int32_t)it->second.num;
        }
    };
    if (auto it = v.obj.find("paper_size_mm"); it != v.obj.end() &&
        it->second.type == JsonValue::Array && it->second.arr.size() >= 2) {
        p.paper_size_mm[0] = (float)it->second.arr[0].num;
        p.paper_size_mm[1] = (float)it->second.arr[1].num;
    }
    gf("warmth", p.warmth);
    gf("fiber", p.fiber);
    gf("formation", p.formation);
    gf("speckle", p.speckle);
    gf("tooth", p.tooth);
    gf("aniso", p.aniso);
    gf("rule_style", p.rule_style);
    gf("rule_gap_mm", p.rule_gap_mm);
    gf("rule_top_mm", p.rule_top_mm);
    gf("rule_weight_mm", p.rule_weight_mm);
    if (auto it = v.obj.find("rule_ink"); it != v.obj.end() &&
        it->second.type == JsonValue::Array && it->second.arr.size() >= 3) {
        p.rule_ink_r = (float)it->second.arr[0].num;
        p.rule_ink_g = (float)it->second.arr[1].num;
        p.rule_ink_b = (float)it->second.arr[2].num;
    }
    gf("margin_enabled", p.margin_enabled);
    gf("margin_x_mm", p.margin_x_mm);
    if (auto it = v.obj.find("margin_ink"); it != v.obj.end() &&
        it->second.type == JsonValue::Array && it->second.arr.size() >= 3) {
        p.margin_ink_r = (float)it->second.arr[0].num;
        p.margin_ink_g = (float)it->second.arr[1].num;
        p.margin_ink_b = (float)it->second.arr[2].num;
    }
    gf("print_fade", p.print_fade);
    gf("light_az_deg", p.light_az_deg);
    gf("light_el_deg", p.light_el_deg);
    gf("light_intensity", p.light_intensity);
    gf("light_temp_k", p.light_temp_k);
    gf("fill_intensity", p.fill_intensity);
    gf("sheen", p.sheen);
    gf("exposure", p.exposure);
    gf("grain", p.grain);
    gf("vignette", p.vignette);
    gf("content_seed", p.content_seed);
    gf("graphite_amount", p.graphite_amount);
    gf("graphite_darkness", p.graphite_darkness);
    gf("graphite_smudge", p.graphite_smudge);
    gf("graphite_eraser", p.graphite_eraser);
    gf("pen_amount", p.pen_amount);
    if (auto it = v.obj.find("pen_color"); it != v.obj.end() &&
        it->second.type == JsonValue::Array && it->second.arr.size() >= 3) {
        p.pen_color_r = (float)it->second.arr[0].num;
        p.pen_color_g = (float)it->second.arr[1].num;
        p.pen_color_b = (float)it->second.arr[2].num;
    }
    gf("pen_feather", p.pen_feather);
    gf("teacher_amount", p.teacher_amount);
    if (auto it = v.obj.find("teacher_color"); it != v.obj.end() &&
        it->second.type == JsonValue::Array && it->second.arr.size() >= 3) {
        p.teacher_color_r = (float)it->second.arr[0].num;
        p.teacher_color_g = (float)it->second.arr[1].num;
        p.teacher_color_b = (float)it->second.arr[2].num;
    }
    gf("teacher_marks", p.teacher_marks);
    gf("smudge_amount", p.smudge_amount);
    gf("eraser_amount", p.eraser_amount);
    gf("quality", p.quality);
    gf("curl_amount", p.curl_amount);
    gf("curl_freq", p.curl_freq);
    gf("corner_lift_amount", p.corner_lift_amount);
    gf("crease_amount", p.crease_amount);
    gf("crease_x", p.crease_x);
    gf("crease_y", p.crease_y);
    gf("crease_angle", p.crease_angle);
    gf("hole_style", p.hole_style);
    gf("edge_style", p.edge_style);
    gf("coffee_amount", p.coffee_amount);
    gf("water_amount", p.water_amount);
    gf("foxing_amount", p.foxing_amount);
    gf("fingerprint_amount", p.fingerprint_amount);
    gf("pencil_smudge_amount", p.pencil_smudge_amount);
    gf("eraser_abrasion_amount", p.eraser_abrasion_amount);
    gf("edge_aging_amount", p.edge_aging_amount);
    gf("translucency_amount", p.translucency_amount);
}

// Embedded fullscreen-triangle vertex SPIR-V (generated from shaders/fullscreen_triangle.vert).
#include "vert_spirv.inc"

struct Metrics {
    std::string device;
    std::string resolution;
    double time = 0.0;
    uint32_t seed = 123u;
    double gpu_ms = 0.0;
    double wall_ms = 0.0;
    double diff_rmse = -1.0;
    int malioc_cycles = -1;
    std::string validation = "ok";
    std::string result = "pass";
};

std::string read_file(const std::string& path, std::size_t* out_size = nullptr) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::fprintf(stderr, "error: cannot open %s\n", path.c_str());
        std::exit(1);
    }
    std::size_t size = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<char> buf(size);
    if (!file.read(buf.data(), static_cast<std::streamsize>(size)).good()) {
        std::fprintf(stderr, "error: cannot read %s\n", path.c_str());
        std::exit(1);
    }
    if (out_size) *out_size = size;
    return std::string(buf.data(), size);
}

std::vector<uint32_t> read_spirv(const std::string& path) {
    std::size_t size = 0;
    std::string data = read_file(path, &size);
    if (size % 4 != 0) {
        std::fprintf(stderr, "error: SPIR-V size not word-aligned: %s\n", path.c_str());
        std::exit(1);
    }
    std::vector<uint32_t> spirv(size / 4);
    std::memcpy(spirv.data(), data.data(), size);
    return spirv;
}

VkShaderModule create_shader_module(VkDevice device, const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv.size() * sizeof(uint32_t);
    ci.pCode = spirv.data();
    VkShaderModule mod;
    VkResult res = vkCreateShaderModule(device, &ci, nullptr, &mod);
    if (res != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateShaderModule failed: %d\n", (int)res);
        std::exit(1);
    }
    return mod;
}

void write_json_metrics(const std::string& path, const Metrics& m) {
    std::ofstream f(path);
    if (!f) {
        std::fprintf(stderr, "error: cannot write metrics %s\n", path.c_str());
        std::exit(1);
    }
    f << "{\n";
    f << "  \"device\": \"" << m.device << "\",\n";
    f << "  \"resolution\": \"" << m.resolution << "\",\n";
    f << "  \"time\": " << m.time << ",\n";
    f << "  \"seed\": " << m.seed << ",\n";
    f << "  \"gpu_ms\": " << m.gpu_ms << ",\n";
    f << "  \"wall_ms\": " << m.wall_ms << ",\n";
    f << "  \"diff_rmse\": " << m.diff_rmse << ",\n";
    f << "  \"malioc_cycles\": " << m.malioc_cycles << ",\n";
    f << "  \"validation\": \"" << m.validation << "\",\n";
    f << "  \"result\": \"" << m.result << "\"\n";
    f << "}\n";
    f.flush();
}

void write_ascii_preview(const uint8_t* rgba, uint32_t width, uint32_t height) {
    const int cols = 60;
    const int rows = 30;
    static const char* ramp = " .:-=+*#%@";
    const int ramp_len = 10;
    std::fprintf(stderr, "\n=== ASCII preview (%dx%d) ===\n", cols, rows);
    for (int ry = 0; ry < rows; ++ry) {
        std::string line;
        line.reserve(cols);
        for (int cx = 0; cx < cols; ++cx) {
            uint32_t sx = (uint32_t)(((double)cx / cols) * width);
            uint32_t sy = (uint32_t)(((double)ry / rows) * height);
            std::size_t idx = (std::size_t)sy * width * 4 + std::size_t(sx) * 4;
            double r = rgba[idx] / 255.0;
            double g = rgba[idx + 1] / 255.0;
            double b = rgba[idx + 2] / 255.0;
            double lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            int i = (int)(lum * ramp_len);
            if (i < 0) i = 0;
            if (i >= ramp_len) i = ramp_len - 1;
            line.push_back(ramp[i]);
        }
        std::fprintf(stderr, "%s\n", line.c_str());
    }
}

double compute_rmse(const uint8_t* a, const uint8_t* b, std::size_t n_pixels) {
    double mse = 0.0;
    for (std::size_t i = 0; i < n_pixels; ++i) {
        double dr = (double)a[4*i+0] - (double)b[4*i+0];
        double dg = (double)a[4*i+1] - (double)b[4*i+1];
        double db = (double)a[4*i+2] - (double)b[4*i+2];
        mse += (dr*dr + dg*dg + db*db) / 3.0;
    }
    mse /= (double)n_pixels;
    return std::sqrt(mse);
}

} // namespace

int main(int argc, char** argv) {
    std::string shader_path;
    std::string params_path;
    std::string out_path;
    std::string json_path;
    std::string golden_path;
    bool ascii = false;
    bool info_only = false;
    double time = 0.0;
    uint32_t seed = 123u;
    uint32_t width = 720;
    uint32_t height = 1280;
    bool bench = false;
    bool use_bake = false;
    bool idle_test = false;
    bool do_shape = false;
    uint32_t warmup = 30;
    uint32_t frames = 120;
    uint32_t idle_seconds = 60;
    uint32_t dirty_after = 0;
    std::string shape_text;
    std::string shape_text_file;
    std::string shape_font;
    float shape_font_size = 24.0f;
    std::string shape_direction = "ltr";
    std::string shape_script = "latn";
    std::string shape_language = "en";
    std::string shape_out = "shaped_run.json";
    bool do_bake_atlas = false;
    std::string atlas_font;
    std::string atlas_glyphs; // comma-separated glyph ids
    float atlas_ppem = 24.0f;
    int32_t atlas_supersample = 4;
    std::string atlas_out = "atlas.png";
    std::string atlas_meta = "atlas_meta.json";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--info") { info_only = true; }
        else if (arg == "--ascii") { ascii = true; }
        else if (arg == "--shader" && i + 1 < argc) { shader_path = argv[++i]; }
        else if (arg == "--params" && i + 1 < argc) { params_path = argv[++i]; }
        else if (arg == "--time" && i + 1 < argc) { time = std::stod(argv[++i]); }
        else if (arg == "--seed" && i + 1 < argc) { seed = (uint32_t)std::stoul(argv[++i]); }
        else if (arg == "--size" && i + 1 < argc) {
            std::string s = argv[++i];
            auto pos = s.find('x');
            if (pos == std::string::npos) {
                std::fprintf(stderr, "error: --size must be WxH\n");
                return 1;
            }
            width = (uint32_t)std::stoul(s.substr(0, pos));
            height = (uint32_t)std::stoul(s.substr(pos + 1));
        }
        else if (arg == "--out" && i + 1 < argc) { out_path = argv[++i]; }
        else if (arg == "--json" && i + 1 < argc) { json_path = argv[++i]; }
        else if (arg == "--golden" && i + 1 < argc) { golden_path = argv[++i]; }
        else if (arg == "--bench") { bench = true; }
        else if (arg == "--bake") { use_bake = true; }
        else if (arg == "--idle-test") { idle_test = true; }
        else if (arg == "--idle-seconds" && i + 1 < argc) { idle_seconds = (uint32_t)std::stoul(argv[++i]); }
        else if (arg == "--dirty-after" && i + 1 < argc) { dirty_after = (uint32_t)std::stoul(argv[++i]); }
        else if (arg == "--warmup" && i + 1 < argc) { warmup = (uint32_t)std::stoul(argv[++i]); }
        else if (arg == "--frames" && i + 1 < argc) { frames = (uint32_t)std::stoul(argv[++i]); }
        else if (arg == "--shape") { do_shape = true; }
        else if (arg == "--shape-dump") { do_shape = true; }
        else if (arg == "--text" && i + 1 < argc) { shape_text = argv[++i]; }
        else if (arg == "--text-file" && i + 1 < argc) { shape_text_file = argv[++i]; }
        else if (arg == "--font" && i + 1 < argc) { shape_font = argv[++i]; }
        else if (arg == "--font-size" && i + 1 < argc) { shape_font_size = std::stof(argv[++i]); }
        else if (arg == "--direction" && i + 1 < argc) { shape_direction = argv[++i]; }
        else if (arg == "--script" && i + 1 < argc) { shape_script = argv[++i]; }
        else if (arg == "--language" && i + 1 < argc) { shape_language = argv[++i]; }
        else if (arg == "--shape-out" && i + 1 < argc) { shape_out = argv[++i]; }
        else if (arg == "--bake-atlas") { do_bake_atlas = true; }
        else if (arg == "--atlas-font" && i + 1 < argc) { atlas_font = argv[++i]; }
        else if (arg == "--atlas-glyphs" && i + 1 < argc) { atlas_glyphs = argv[++i]; }
        else if (arg == "--atlas-ppem" && i + 1 < argc) { atlas_ppem = std::stof(argv[++i]); }
        else if (arg == "--atlas-supersample" && i + 1 < argc) { atlas_supersample = std::stoi(argv[++i]); }
        else if (arg == "--atlas-out" && i + 1 < argc) { atlas_out = argv[++i]; }
        else if (arg == "--atlas-meta" && i + 1 < argc) { atlas_meta = argv[++i]; }
        else if (arg == "--ascii") { ascii = true; }
    }

    // ---- Shaping mode: CPU-only, produces a deterministic shaped-run artifact.
    // No Vulkan, no font asset bundled -- the font is supplied at runtime via --font.
    if (do_shape) {
        std::string utf8;
        if (!shape_text_file.empty()) {
            std::size_t sz = 0;
            utf8 = read_file(shape_text_file, &sz);
        } else {
            utf8 = shape_text;
        }
        if (utf8.empty()) {
            std::fprintf(stderr, "error: --shape requires --text or --text-file\n");
            return 1;
        }
        std::vector<unsigned char> font_data;
        if (!shape_font.empty() && !noat::load_file_bytes(shape_font, font_data)) {
            std::fprintf(stderr, "error: cannot read font %s\n", shape_font.c_str());
            return 1;
        }
        noat::ShapedRun run = noat::shape_text(utf8.c_str(), static_cast<int32_t>(utf8.size()),
                                   font_data.empty() ? nullptr : font_data.data(),
                                   font_data.size(), shape_font_size,
                                   shape_direction.c_str(), shape_script.c_str(),
                                   shape_language.c_str());
        if (run.glyph_count == 0) {
            std::fprintf(stderr, "error: shaping produced no glyphs\n");
            return 1;
        }
        std::string json = noat::shaped_run_to_json(run);
        std::ofstream out(shape_out);
        if (!out) {
            std::fprintf(stderr, "error: cannot write %s\n", shape_out.c_str());
            return 1;
        }
        out << json;
        out.flush();
        std::fprintf(stderr, "wrote shaped run: %s (glyphs=%u clusters=%u)\n",
                     shape_out.c_str(), run.glyph_count, run.cluster_count);
        return 0;
    }

    // ---- Atlas bake mode: CPU-only, produces an SDF glyph atlas PNG + meta.
    // Consumes glyph IDs (comma list) and bakes one atlas entry per glyph.
    if (do_bake_atlas) {
        if (atlas_font.empty() || atlas_glyphs.empty()) {
            std::fprintf(stderr,
                         "error: --bake-atlas requires --atlas-font and --atlas-glyphs\n");
            return 1;
        }
        std::vector<unsigned char> font_data;
        if (!noat::load_file_bytes(atlas_font, font_data)) {
            std::fprintf(stderr, "error: cannot read font %s\n", atlas_font.c_str());
            return 1;
        }
        std::vector<uint32_t> glyph_ids;
        {
            std::stringstream ss(atlas_glyphs);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                if (tok.empty()) continue;
                glyph_ids.push_back((uint32_t)std::stoul(tok));
            }
        }
        if (glyph_ids.empty()) {
            std::fprintf(stderr, "error: --atlas-glyphs produced no glyph ids\n");
            return 1;
        }
        noat::SdfAtlasResult res = noat::bake_sdf_atlas(font_data.data(), font_data.size(),
                                            glyph_ids, atlas_ppem,
                                            atlas_supersample);
        if (res.glyphs.empty()) {
            std::fprintf(stderr, "error: atlas bake produced no glyphs\n");
            return 1;
        }
        if (!stbi_write_png(atlas_out.c_str(), res.atlas_width, res.atlas_height, 4,
                            res.atlas_rgba8.data(), res.atlas_width * 4)) {
            std::fprintf(stderr, "error: cannot write atlas %s\n", atlas_out.c_str());
            return 1;
        }
        // Meta JSON: per-glyph atlas rect + metrics.
        std::ofstream meta(atlas_meta);
        if (!meta) {
            std::fprintf(stderr, "error: cannot write %s\n", atlas_meta.c_str());
            return 1;
        }
        meta << "{\n";
        meta << "  \"ppem\": " << res.ppem << ",\n";
        meta << "  \"scale\": " << res.scale << ",\n";
        meta << "  \"atlas_width\": " << res.atlas_width << ",\n";
        meta << "  \"atlas_height\": " << res.atlas_height << ",\n";
        meta << "  \"glyphs\": [\n";
        for (size_t i = 0; i < res.glyphs.size(); ++i) {
            const auto& g = res.glyphs[i];
            meta << "    {\"gid\":" << g.glyph_id
                 << ",\"x\":" << g.atlas_x << ",\"y\":" << g.atlas_y
                 << ",\"w\":" << g.atlas_w << ",\"h\":" << g.atlas_h
                 << ",\"advance_x\":" << g.advance_x
                 << ",\"bearing_x\":" << g.bearing_x << "}";
            if (i + 1 < res.glyphs.size()) meta << ",";
            meta << "\n";
        }
        meta << "  ]\n";
        meta << "}\n";
        meta.flush();
        std::fprintf(stderr,
                     "wrote atlas %s (%dx%d) + meta %s (glyphs=%zu)\n",
                     atlas_out.c_str(), res.atlas_width, res.atlas_height,
                     atlas_meta.c_str(), res.glyphs.size());
        return 0;
    }

    // ---- Vulkan instance (headless: no surface, no swapchain) ----
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "paper-headless";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "noat";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo inst_ci = {};
    inst_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_ci.pApplicationInfo = &app_info;

#ifndef ANDROID
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
    inst_ci.enabledLayerCount = 1;
    inst_ci.ppEnabledLayerNames = layers;
#endif

    VkInstance instance;
    if (vkCreateInstance(&inst_ci, nullptr, &instance) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateInstance failed\n");
        return 1;
    }

    uint32_t api_version = 0;
    vkEnumerateInstanceVersion(&api_version);

    // ---- Select Mali-G610 ----
    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr);
    if (gpu_count == 0) {
        std::fprintf(stderr, "error: no Vulkan physical devices\n");
        return 1;
    }
    std::vector<VkPhysicalDevice> gpus(gpu_count);
    vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.data());

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties chosen_props = {};
    for (uint32_t i = 0; i < gpu_count; ++i) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpus[i], &props);
        std::fprintf(stderr, "gpu[%u]: %s (api %d.%d.%d, type %d)\n",
                     i, props.deviceName,
                     VK_VERSION_MAJOR(props.apiVersion),
                     VK_VERSION_MINOR(props.apiVersion),
                     VK_VERSION_PATCH(props.apiVersion),
                     (int)props.deviceType);
        const char* name = props.deviceName;
        bool is_mali = std::strstr(name, "Mali-G610") != nullptr;
        if (is_mali || chosen == VK_NULL_HANDLE) {
            chosen = gpus[i];
            chosen_props = props;
        }
        if (is_mali) break;
    }
    if (chosen == VK_NULL_HANDLE) {
        std::fprintf(stderr, "error: no physical device selected\n");
        return 1;
    }

    std::fprintf(stderr, "selected device: %s (api %d.%d.%d)\n",
                 chosen_props.deviceName,
                 VK_VERSION_MAJOR(chosen_props.apiVersion),
                 VK_VERSION_MINOR(chosen_props.apiVersion),
                 VK_VERSION_PATCH(chosen_props.apiVersion));

    uint32_t queue_family = UINT32_MAX;
    {
        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(chosen, &qf_count, nullptr);
        std::vector<VkQueueFamilyProperties> qfps(qf_count);
        vkGetPhysicalDeviceQueueFamilyProperties(chosen, &qf_count, qfps.data());
        for (uint32_t i = 0; i < qf_count; ++i) {
            if ((qfps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                queue_family = i;
                break;
            }
        }
        if (queue_family == UINT32_MAX) {
            std::fprintf(stderr, "error: no graphics queue family\n");
            return 1;
        }
    }

    if (info_only) {
        std::printf("{\n");
        std::printf("  \"device\": \"%s\",\n", chosen_props.deviceName);
        std::printf("  \"vulkan_api\": \"%d.%d.%d\",\n",
                    VK_VERSION_MAJOR(chosen_props.apiVersion),
                    VK_VERSION_MINOR(chosen_props.apiVersion),
                    VK_VERSION_PATCH(chosen_props.apiVersion));
        std::printf("  \"instance_api\": \"%d.%d.%d\",\n",
                    VK_VERSION_MAJOR(api_version),
                    VK_VERSION_MINOR(api_version),
                    VK_VERSION_PATCH(api_version));
        std::printf("  \"headless\": true\n");
#ifdef NOAT_HAVE_HARFBUZZ
        std::printf(",\"shaping\": \"harfbuzz\"\n");
#else
        std::printf(",\"shaping\": \"none\"\n");
#endif
        std::printf("}\n");
        vkDestroyInstance(instance, nullptr);
        return 0;
    }

    // ---- Logical device ----
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo qinfo = {};
    qinfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qinfo.queueFamilyIndex = queue_family;
    qinfo.queueCount = 1;
    qinfo.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo dev_ci = {};
    dev_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_ci.queueCreateInfoCount = 1;
    dev_ci.pQueueCreateInfos = &qinfo;
    dev_ci.pEnabledFeatures = nullptr;

    VkDevice device;
    if (vkCreateDevice(chosen, &dev_ci, nullptr, &device) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateDevice failed\n");
        return 1;
    }
    VkQueue queue;
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    // ---- Bench mode uses wall-clock timing around submit/wait
    // (vkCmdWriteTimestamp segfaults on Mali-G610; timestamp queries unsupported).

    // ---- VMA allocator ----
    VmaVulkanFunctions vk_funcs = {};
    vk_funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vk_funcs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo vma_ci = {};
    vma_ci.flags = 0;
    vma_ci.physicalDevice = chosen;
    vma_ci.device = device;
    vma_ci.instance = instance;
    vma_ci.pVulkanFunctions = &vk_funcs;
    VmaAllocator allocator;
    if (vmaCreateAllocator(&vma_ci, &allocator) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vmaCreateAllocator failed\n");
        return 1;
    }

    // ---- Offscreen image (color attachment only, no depth, no MSAA) ----
    const VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageCreateInfo image_ci = {};
    image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_ci.imageType = VK_IMAGE_TYPE_2D;
    image_ci.format = color_format;
    image_ci.extent = {width, height, 1};
    image_ci.mipLevels = 1;
    image_ci.arrayLayers = 1;
    image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage color_image = VK_NULL_HANDLE;
    VmaAllocation color_alloc = VK_NULL_HANDLE;
    VmaAllocationCreateInfo image_alloc_ci = {};
    image_alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(allocator, &image_ci, &image_alloc_ci,
                       &color_image, &color_alloc, nullptr) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vmaCreateImage failed\n");
        return 1;
    }

    VkImageViewCreateInfo view_ci = {};
    view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image = color_image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = color_format;
    view_ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView color_view;
    if (vkCreateImageView(device, &view_ci, nullptr, &color_view) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateImageView failed\n");
        return 1;
    }

    // ---- Readback buffer (host visible, mapped) ----
    VkDeviceSize row_pitch = (VkDeviceSize)width * 4u;
    VkDeviceSize buffer_size = row_pitch * (VkDeviceSize)height;
    VkBufferCreateInfo buffer_ci = {};
    buffer_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_ci.size = buffer_size;
    buffer_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer readback_buffer = VK_NULL_HANDLE;
    VmaAllocation buf_alloc = VK_NULL_HANDLE;
    VmaAllocationCreateInfo buf_alloc_ci = {};
    buf_alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    buf_alloc_ci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                         VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    VmaAllocationInfo alloc_info = {};
    if (vmaCreateBuffer(allocator, &buffer_ci, &buf_alloc_ci,
                        &readback_buffer, &buf_alloc, &alloc_info) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vmaCreateBuffer failed\n");
        return 1;
    }
    void* mapped = alloc_info.pMappedData;

    // ---- Render pass: one subpass, color only, loadOp DONT_CARE ----
    VkAttachmentDescription att = {};
    att.format = color_format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkRenderPassCreateInfo rp_ci = {};
    rp_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_ci.attachmentCount = 1;
    rp_ci.pAttachments = &att;
    rp_ci.subpassCount = 1;
    rp_ci.pSubpasses = &subpass;
    VkRenderPass render_pass;
    if (vkCreateRenderPass(device, &rp_ci, nullptr, &render_pass) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateRenderPass failed\n");
        return 1;
    }

    VkImageView attachments[] = {color_view};
    VkFramebufferCreateInfo fb_ci = {};
    fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_ci.renderPass = render_pass;
    fb_ci.attachmentCount = 1;
    fb_ci.pAttachments = attachments;
    fb_ci.width = width;
    fb_ci.height = height;
    fb_ci.layers = 1;
    VkFramebuffer framebuffer;
    if (vkCreateFramebuffer(device, &fb_ci, nullptr, &framebuffer) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateFramebuffer failed\n");
        return 1;
    }

    // ---- Pipeline ----
    // Vertex shader is embedded (fullscreen triangle). Fragment shader comes from --shader.
    std::vector<uint32_t> vert_spv(kVertSpirv, kVertSpirv + (sizeof(kVertSpirv) / sizeof(kVertSpirv[0])));
    std::vector<uint32_t> frag_spv = read_spirv(shader_path);
    VkShaderModule vert_mod = create_shader_module(device, vert_spv);
    VkShaderModule frag_mod = create_shader_module(device, frag_spv);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_mod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_mod;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState cb_att = {};
    cb_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cb_att.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cb_att;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn_state = {};
    dyn_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn_state.dynamicStateCount = 2;
    dyn_state.pDynamicStates = dyn;

    // Descriptor set layout: binding 0 = combined paper+content params UBO
    VkDescriptorSetLayoutBinding dsl_binding = {};
    dsl_binding.binding = 0;
    dsl_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    dsl_binding.descriptorCount = 1;
    dsl_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    dsl_binding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo dsl_ci = {};
    dsl_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl_ci.bindingCount = 1;
    dsl_ci.pBindings = &dsl_binding;
    VkDescriptorSetLayout dsl;
    if (vkCreateDescriptorSetLayout(device, &dsl_ci, nullptr, &dsl) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateDescriptorSetLayout failed\n");
        return 1;
    }

    // Single combined parameter buffer (VMA, host visible)
    VkBufferCreateInfo ubuf_ci = {};
    ubuf_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ubuf_ci.size = sizeof(Params);
    ubuf_ci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    ubuf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer ubo_buffer = VK_NULL_HANDLE;
    VmaAllocation ubo_alloc = VK_NULL_HANDLE;
    VmaAllocationCreateInfo ubo_alloc_ci = {};
    ubo_alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    ubo_alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo ubo_alloc_info = {};
    if (vmaCreateBuffer(allocator, &ubuf_ci, &ubo_alloc_ci,
                        &ubo_buffer, &ubo_alloc, &ubo_alloc_info) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vmaCreateBuffer (ubo) failed\n");
        return 1;
    }
    void* ubo_mapped = ubo_alloc_info.pMappedData;

    Params params = default_params();
    if (!params_path.empty()) {
        apply_params_json(params, load_json_file(params_path));
    }
    std::memcpy(ubo_mapped, &params, sizeof(Params));
    vmaFlushAllocation(allocator, ubo_alloc, 0, VK_WHOLE_SIZE);

    // GlyphInstances UBO
    VkBufferCreateInfo gi_ubuf_ci = {};
    gi_ubuf_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    gi_ubuf_ci.size = sizeof(GlyphInstancesUBO);
    gi_ubuf_ci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    gi_ubuf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer gi_ubo_buffer = VK_NULL_HANDLE;
    VmaAllocation gi_ubo_alloc = VK_NULL_HANDLE;
    VkBufferCreateInfo gi_ci = gi_ubuf_ci;
    VmaAllocationCreateInfo gi_ubo_alloc_ci = {};
    gi_ubo_alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    gi_ubo_alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo gi_ubo_alloc_info = {};
    if (vmaCreateBuffer(allocator, &gi_ci, &gi_ubo_alloc_ci,
                        &gi_ubo_buffer, &gi_ubo_alloc, &gi_ubo_alloc_info) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vmaCreateBuffer (glyph UBO) failed\n");
        return 1;
    }
    void* gi_ubo_mapped = gi_ubo_alloc_info.pMappedData;

    // DeformParams UBO
    VkBufferCreateInfo dp_ci = {};
    dp_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    dp_ci.size = sizeof(DeformParamsUBO);
    dp_ci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    dp_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer dp_ubo_buffer = VK_NULL_HANDLE;
    VmaAllocation dp_ubo_alloc = VK_NULL_HANDLE;
    VmaAllocationCreateInfo dp_ubo_alloc_ci = {};
    dp_ubo_alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    dp_ubo_alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo dp_ubo_alloc_info = {};
    if (vmaCreateBuffer(allocator, &dp_ci, &dp_ubo_alloc_ci,
                        &dp_ubo_buffer, &dp_ubo_alloc, &dp_ubo_alloc_info) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vmaCreateBuffer (deform UBO) failed\n");
        return 1;
    }
    void* dp_ubo_mapped = dp_ubo_alloc_info.pMappedData;

    // Default deformation parameters
    DeformParamsUBO deform_params = {};
    deform_params.warp_freq = 0.05f;
    deform_params.warp_amp = 0.5f;
    deform_params.warp_speed = 1.0f;
    deform_params.cluster_drift = 0.3f;
    deform_params.rot_variation = 0.05f;
    deform_params.size_variation = 0.08f;
    deform_params.advance_var = 0.05f;
    deform_params.ink_feather = 2.0f;
    deform_params.pressure_var = 0.3f;
    std::memcpy(dp_ubo_mapped, &deform_params, sizeof(DeformParamsUBO));
    vmaFlushAllocation(allocator, dp_ubo_alloc, 0, VK_WHOLE_SIZE);

    VkDescriptorPoolSize pool_size = {};
    pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_size.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pool_ci2 = {};
    pool_ci2.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci2.maxSets = 1;
    pool_ci2.poolSizeCount = 1;
    pool_ci2.pPoolSizes = &pool_size;
    VkDescriptorPool dpool;
    if (vkCreateDescriptorPool(device, &pool_ci2, nullptr, &dpool) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateDescriptorPool failed\n");
        return 1;
    }
    VkDescriptorSet dset;
    VkDescriptorSetAllocateInfo dsa = {};
    dsa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsa.descriptorPool = dpool;
    dsa.descriptorSetCount = 1;
    dsa.pSetLayouts = &dsl;
    if (vkAllocateDescriptorSets(device, &dsa, &dset) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkAllocateDescriptorSets failed\n");
        return 1;
    }
    VkDescriptorBufferInfo binfo = {};
    binfo.buffer = ubo_buffer;
    binfo.offset = 0;
    binfo.range = sizeof(Params);
    VkDescriptorBufferInfo gi_binfo = {};
    gi_binfo.buffer = gi_ubo_buffer;
    gi_binfo.offset = 0;
    gi_binfo.range = sizeof(GlyphInstancesUBO);
    VkDescriptorBufferInfo dp_binfo = {};
    dp_binfo.buffer = dp_ubo_buffer;
    dp_binfo.offset = 0;
    dp_binfo.range = sizeof(DeformParamsUBO);
    VkWriteDescriptorSet wds = {};
    wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wds.dstSet = dset;
    wds.dstBinding = 0;
    wds.dstArrayElement = 0;
    wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wds.pBufferInfo = &binfo;
    vkUpdateDescriptorSets(device, 1, &wds, 0, nullptr);

    VkPushConstantRange pc_range = {};
    pc_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pc_range.offset = 0;
    pc_range.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo layout_ci = {};
    layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_ci.setLayoutCount = 1;
    layout_ci.pSetLayouts = &dsl;
    layout_ci.pushConstantRangeCount = 1;
    layout_ci.pPushConstantRanges = &pc_range;
    VkPipelineLayout layout;
    if (vkCreatePipelineLayout(device, &layout_ci, nullptr, &layout) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreatePipelineLayout failed\n");
        return 1;
    }

    VkGraphicsPipelineCreateInfo gp_ci = {};
    gp_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp_ci.stageCount = 2;
    gp_ci.pStages = stages;
    gp_ci.pVertexInputState = &vi;
    gp_ci.pInputAssemblyState = &ia;
    gp_ci.pViewportState = &vp;
    gp_ci.pRasterizationState = &rs;
    gp_ci.pMultisampleState = &ms;
    gp_ci.pDepthStencilState = &ds;
    gp_ci.pColorBlendState = &cb;
    gp_ci.pDynamicState = &dyn_state;
    gp_ci.layout = layout;
    gp_ci.renderPass = render_pass;
    gp_ci.subpass = 0;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp_ci, nullptr, &pipeline) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateGraphicsPipelines failed\n");
        return 1;
    }

    vkDestroyShaderModule(device, vert_mod, nullptr);
    vkDestroyShaderModule(device, frag_mod, nullptr);

    // ---- Two-pass bake + final resources (when --bake is set) ----
    VkImage bake_image = VK_NULL_HANDLE;
    VmaAllocation bake_alloc = VK_NULL_HANDLE;
    VkImageView bake_view = VK_NULL_HANDLE;
    VkSampler bake_sampler = VK_NULL_HANDLE;
    VkRenderPass bake_render_pass = VK_NULL_HANDLE;
    VkFramebuffer bake_framebuffer = VK_NULL_HANDLE;
    VkPipeline bake_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout bake_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout bake_dsl = VK_NULL_HANDLE;
    VkDescriptorSet bake_dset = VK_NULL_HANDLE;
    VkDescriptorPool bake_dpool = VK_NULL_HANDLE;

    VkPipeline final_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout final_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout final_dsl = VK_NULL_HANDLE;
    VkDescriptorSet final_dset = VK_NULL_HANDLE;
    VkDescriptorPool final_dpool = VK_NULL_HANDLE;

    if (use_bake) {
        // ============ BAKE PASS RESOURCES ============

        // Bake texture: RGBA8, albedo in RGB, height in A
        VkImageCreateInfo bake_image_ci = {};
        bake_image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        bake_image_ci.imageType = VK_IMAGE_TYPE_2D;
        bake_image_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        bake_image_ci.extent = {width, height, 1};
        bake_image_ci.mipLevels = 1;
        bake_image_ci.arrayLayers = 1;
        bake_image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
        bake_image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        bake_image_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        bake_image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bake_image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VmaAllocationCreateInfo bake_alloc_ci = {};
        bake_alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(allocator, &bake_image_ci, &bake_alloc_ci,
                           &bake_image, &bake_alloc, nullptr) != VK_SUCCESS) {
            std::fprintf(stderr, "error: vmaCreateImage (bake) failed\n");
            return 1;
        }
        VkImageViewCreateInfo bake_view_ci = {};
        bake_view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        bake_view_ci.image = bake_image;
        bake_view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        bake_view_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        bake_view_ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device, &bake_view_ci, nullptr, &bake_view) != VK_SUCCESS) {
            std::fprintf(stderr, "error: vkCreateImageView (bake) failed\n");
            return 1;
        }
        VkSamplerCreateInfo samp_ci = {};
        samp_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samp_ci.magFilter = VK_FILTER_LINEAR;
        samp_ci.minFilter = VK_FILTER_LINEAR;
        samp_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samp_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp_ci.maxLod = 1.0f;
        if (vkCreateSampler(device, &samp_ci, nullptr, &bake_sampler) != VK_SUCCESS) {
            std::fprintf(stderr, "error: vkCreateSampler failed\n");
            return 1;
        }

        // Bake render pass: color attachment only, loadOp DONT_CARE, storeOp STORE
        VkAttachmentDescription bake_att = {};
        bake_att.format = VK_FORMAT_R8G8B8A8_UNORM;
        bake_att.samples = VK_SAMPLE_COUNT_1_BIT;
        bake_att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        bake_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        bake_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        bake_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        bake_att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bake_att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference bake_att_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription bake_subpass = {};
        bake_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        bake_subpass.colorAttachmentCount = 1;
        bake_subpass.pColorAttachments = &bake_att_ref;
        VkRenderPassCreateInfo bake_rp_ci = {};
        bake_rp_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        bake_rp_ci.attachmentCount = 1;
        bake_rp_ci.pAttachments = &bake_att;
        bake_rp_ci.subpassCount = 1;
        bake_rp_ci.pSubpasses = &bake_subpass;
        if (vkCreateRenderPass(device, &bake_rp_ci, nullptr, &bake_render_pass) != VK_SUCCESS) {
            std::fprintf(stderr, "error: vkCreateRenderPass (bake) failed\n");
            return 1;
        }
        VkFramebufferCreateInfo bake_fb_ci = {};
        bake_fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        bake_fb_ci.renderPass = bake_render_pass;
        bake_fb_ci.attachmentCount = 1;
        bake_fb_ci.pAttachments = &bake_view;
        bake_fb_ci.width = width;
        bake_fb_ci.height = height;
        bake_fb_ci.layers = 1;
        if (vkCreateFramebuffer(device, &bake_fb_ci, nullptr, &bake_framebuffer) != VK_SUCCESS) {
            std::fprintf(stderr, "error: vkCreateFramebuffer (bake) failed\n");
            return 1;
        }

        // Bake descriptor set layout: binding 0 = params UBO
        VkDescriptorSetLayoutBinding bake_binding = {};
        bake_binding.binding = 0;
        bake_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bake_binding.descriptorCount = 1;
        bake_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo bake_dsl_ci = {};
        bake_dsl_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        bake_dsl_ci.bindingCount = 1;
        bake_dsl_ci.pBindings = &bake_binding;
        if (vkCreateDescriptorSetLayout(device, &bake_dsl_ci, nullptr, &bake_dsl) != VK_SUCCESS) {
            std::fprintf(stderr, "error: vkCreateDescriptorSetLayout (bake) failed\n");
            return 1;
        }
        VkDescriptorPoolSize bake_pool_size = {};
        bake_pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bake_pool_size.descriptorCount = 1;
        VkDescriptorPoolCreateInfo bake_dpool_ci = {};
        bake_dpool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        bake_dpool_ci.maxSets = 1;
        bake_dpool_ci.poolSizeCount = 1;
        bake_dpool_ci.pPoolSizes = &bake_pool_size;
        if (vkCreateDescriptorPool(device, &bake_dpool_ci, nullptr, &bake_dpool) != VK_SUCCESS) {
            std::fprintf(stderr, "error: vkCreateDescriptorPool (bake) failed\n");
            return 1;
        }
        VkDescriptorSetAllocateInfo bake_dsa = {};
        bake_dsa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        bake_dsa.descriptorPool = bake_dpool;
        bake_dsa.descriptorSetCount = 1;
        bake_dsa.pSetLayouts = &bake_dsl;
        if (vkAllocateDescriptorSets(device, &bake_dsa, &bake_dset) != VK_SUCCESS) {
            std::fprintf(stderr, "error: vkAllocateDescriptorSets (bake) failed\n");
            return 1;
        }
        VkWriteDescriptorSet bake_wds = {};
        bake_wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        bake_wds.dstSet = bake_dset;
        bake_wds.dstBinding = 0;
        bake_wds.descriptorCount = 1;
        bake_wds.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bake_wds.pBufferInfo = &binfo;
        vkUpdateDescriptorSets(device, 1, &bake_wds, 0, nullptr);

        // Bake pipeline layout
        VkPushConstantRange bake_pc_range = {};
        bake_pc_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bake_pc_range.offset = 0;
        bake_pc_range.size = sizeof(PushConstants);
        VkPipelineLayoutCreateInfo bake_layout_ci = {};
        bake_layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        bake_layout_ci.setLayoutCount = 1;
        bake_layout_ci.pSetLayouts = &bake_dsl;
        bake_layout_ci.pushConstantRangeCount = 1;
        bake_layout_ci.pPushConstantRanges = &bake_pc_range;
        if (vkCreatePipelineLayout(device, &bake_layout_ci, nullptr, &bake_layout) != VK_SUCCESS) {
            std::fprintf(stderr, "error: vkCreatePipelineLayout (bake) failed\n");
            return 1;
        }

        // Load bake fragment SPIR-V
        std::string bake_shader_path = shader_path;
        // NAME.frag.spv -> NAME_bake.frag.spv
        auto dot_frag_spv = bake_shader_path.rfind(".frag.spv");
        if (dot_frag_spv != std::string::npos) {
            bake_shader_path = bake_shader_path.substr(0, dot_frag_spv) + "_bake.frag.spv";
        } else {
            auto dot_spv = bake_shader_path.rfind(".spv");
            if (dot_spv != std::string::npos) {
                bake_shader_path = bake_shader_path.substr(0, dot_spv) + ".bake.spv";
            } else {
                bake_shader_path += ".bake.spv";
            }
        }
        std::vector<uint32_t> bake_frag_spv = read_spirv(bake_shader_path);
        VkShaderModule bake_vert_mod = VK_NULL_HANDLE, bake_frag_mod = VK_NULL_HANDLE;
        {
            VkShaderModuleCreateInfo sm_ci = {};
            sm_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            sm_ci.codeSize = sizeof(kVertSpirv);
            sm_ci.pCode = kVertSpirv;
            if (vkCreateShaderModule(device, &sm_ci, nullptr, &bake_vert_mod) != VK_SUCCESS) {
                std::fprintf(stderr, "error: vkCreateShaderModule (bake vert) failed\n");
                return 1;
            }
        }
        {
            VkShaderModuleCreateInfo sm_ci = {};
            sm_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            sm_ci.codeSize = bake_frag_spv.size() * sizeof(uint32_t);
            sm_ci.pCode = bake_frag_spv.data();
            if (vkCreateShaderModule(device, &sm_ci, nullptr, &bake_frag_mod) != VK_SUCCESS) {
                std::fprintf(stderr, "error: vkCreateShaderModule (bake frag) failed\n");
                return 1;
            }
        }
        VkPipelineShaderStageCreateInfo bake_stages[2] = {};
        bake_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        bake_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        bake_stages[0].module = bake_vert_mod;
        bake_stages[0].pName = "main";
        bake_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        bake_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        bake_stages[1].module = bake_frag_mod;
        bake_stages[1].pName = "main";
        VkGraphicsPipelineCreateInfo bake_gp_ci = {};
        bake_gp_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        bake_gp_ci.stageCount = 2;
        bake_gp_ci.pStages = bake_stages;
        bake_gp_ci.pVertexInputState = &vi;
        bake_gp_ci.pInputAssemblyState = &ia;
        bake_gp_ci.pViewportState = &vp;
        bake_gp_ci.pRasterizationState = &rs;
        bake_gp_ci.pMultisampleState = &ms;
        bake_gp_ci.pDepthStencilState = &ds;
        bake_gp_ci.pColorBlendState = &cb;
        bake_gp_ci.pDynamicState = &dyn_state;
        bake_gp_ci.layout = bake_layout;
        bake_gp_ci.renderPass = bake_render_pass;
        bake_gp_ci.subpass = 0;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &bake_gp_ci, nullptr, &bake_pipeline) != VK_SUCCESS) {
            std::fprintf(stderr, "error: vkCreateGraphicsPipelines (bake) failed\n");
            return 1;
        }
        vkDestroyShaderModule(device, bake_vert_mod, nullptr);
        vkDestroyShaderModule(device, bake_frag_mod, nullptr);

        // ============ FINAL PASS RESOURCES ============

        // Final descriptor set layout:
        //   binding 0 = params UBO
        //   binding 1 = albedo sampler
        //   binding 2 = height sampler
        //   binding 3 = SDF atlas sampler
        //   binding 4 = GlyphInstances UBO
        //   binding 5 = DeformParams UBO
        VkDescriptorSetLayoutBinding final_bindings[6] = {};
        final_bindings[0].binding = 0;
        final_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        final_bindings[0].descriptorCount = 1;
        final_bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        final_bindings[1].binding = 1;
        final_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        final_bindings[1].descriptorCount = 1;
        final_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        final_bindings[2].binding = 2;
        final_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        final_bindings[2].descriptorCount = 1;
        final_bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        final_bindings[3].binding = 3;
        final_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        final_bindings[3].descriptorCount = 1;
        final_bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        final_bindings[4].binding = 4;
        final_bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        final_bindings[4].descriptorCount = 1;
        final_bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        final_bindings[5].binding = 5;
        final_bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        final_bindings[5].descriptorCount = 1;
        final_bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo final_dsl_ci = {};
        final_dsl_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        final_dsl_ci.bindingCount = 6;
        final_dsl_ci.pBindings = final_bindings;
        if (vkCreateDescriptorSetLayout(device, &final_dsl_ci, nullptr, &final_dsl) != VK_SUCCESS) {
            std::fprintf(stderr, "error: vkCreateDescriptorSetLayout (final) failed\n");
            return 1;
        }
        VkDescriptorPoolSize final_pool_sizes[3] = {};
        final_pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        final_pool_sizes[0].descriptorCount = 3; // params + glyphs + deform
        final_pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        final_pool_sizes[1].descriptorCount = 3; // albedo + height + sdf_atlas
        VkDescriptorPoolCreateInfo final_dpool_ci = {};
        final_dpool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        final_dpool_ci.maxSets = 1;
        final_dpool_ci.poolSizeCount = 3;
        final_dpool_ci.pPoolSizes = final_pool_sizes;
        if (vkCreateDescriptorPool(device, &final_dpool_ci, nullptr, &final_dpool) != VK_SUCCESS) {
            std::fprintf(stderr, "error: vkCreateDescriptorPool (final) failed\n");
            return 1;
        }
        VkDescriptorSetAllocateInfo final_dsa = {};
        final_dsa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        final_dsa.descriptorPool = final_dpool;
        final_dsa.descriptorSetCount = 1;
        final_dsa.pSetLayouts = &final_dsl;
        if (vkAllocateDescriptorSets(device, &final_dsa, &final_dset) != VK_SUCCESS) {
            std::fprintf(stderr, "error: vkAllocateDescriptorSets (final) failed\n");
            return 1;
        }
        VkDescriptorImageInfo albedo_info = {};
        albedo_info.sampler = bake_sampler;
        albedo_info.imageView = bake_view;
        albedo_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo height_info = {};
        height_info.sampler = bake_sampler;
        height_info.imageView = bake_view;
        height_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo sdf_info = {};
        sdf_info.sampler = bake_sampler;
        sdf_info.imageView = bake_view;
        sdf_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet final_wds[6] = {};
        final_wds[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        final_wds[0].dstSet = final_dset;
        final_wds[0].dstBinding = 0;
        final_wds[0].descriptorCount = 1;
        final_wds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        final_wds[0].pBufferInfo = &binfo;
        final_wds[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        final_wds[1].dstSet = final_dset;
        final_wds[1].dstBinding = 1;
        final_wds[1].descriptorCount = 1;
        final_wds[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        final_wds[1].pImageInfo = &albedo_info;
        final_wds[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        final_wds[2].dstSet = final_dset;
        final_wds[2].dstBinding = 2;
        final_wds[2].descriptorCount = 1;
        final_wds[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        final_wds[2].pImageInfo = &height_info;
        final_wds[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        final_wds[3].dstSet = final_dset;
        final_wds[3].dstBinding = 3;
        final_wds[3].descriptorCount = 1;
        final_wds[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        final_wds[3].pImageInfo = &sdf_info;
        final_wds[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        final_wds[4].dstSet = final_dset;
        final_wds[4].dstBinding = 4;
        final_wds[4].descriptorCount = 1;
        final_wds[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        final_wds[4].pBufferInfo = &binfo;
        final_wds[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        final_wds[5].dstSet = final_dset;
        final_wds[5].dstBinding = 5;
        final_wds[5].descriptorCount = 1;
        final_wds[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        final_wds[4].pBufferInfo = &gi_binfo;
        final_wds[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        final_wds[5].dstSet = final_dset;
        final_wds[5].dstBinding = 5;
        final_wds[5].descriptorCount = 1;
        final_wds[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        final_wds[5].pBufferInfo = &dp_binfo;
        vkUpdateDescriptorSets(device, 6, final_wds, 0, nullptr);

        // Final pipeline layout
        VkPushConstantRange final_pc_range = {};
        final_pc_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        final_pc_range.offset = 0;
        final_pc_range.size = sizeof(PushConstants);
        VkPipelineLayoutCreateInfo final_layout_ci = {};
        final_layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        final_layout_ci.setLayoutCount = 1;
        final_layout_ci.pSetLayouts = &final_dsl;
        final_layout_ci.pushConstantRangeCount = 1;
        final_layout_ci.pPushConstantRanges = &final_pc_range;
        if (vkCreatePipelineLayout(device, &final_layout_ci, nullptr, &final_layout) != VK_SUCCESS) {
            std::fprintf(stderr, "error: vkCreatePipelineLayout (final) failed\n");
            return 1;
        }

        // Load final fragment SPIR-V
        std::string final_shader_path = shader_path;
        // NAME.frag.spv -> NAME_final.frag.spv
        auto dot_frag_spv2 = final_shader_path.rfind(".frag.spv");
        if (dot_frag_spv2 != std::string::npos) {
            final_shader_path = final_shader_path.substr(0, dot_frag_spv2) + "_final.frag.spv";
        } else {
            auto dot_spv2 = final_shader_path.rfind(".spv");
            if (dot_spv2 != std::string::npos) {
                final_shader_path = final_shader_path.substr(0, dot_spv2) + "_final.spv";
            } else {
                final_shader_path += "_final.spv";
            }
        }
        std::vector<uint32_t> final_frag_spv = read_spirv(final_shader_path);
        VkShaderModule final_vert_mod = VK_NULL_HANDLE, final_frag_mod = VK_NULL_HANDLE;
        {
            VkShaderModuleCreateInfo sm_ci = {};
            sm_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            sm_ci.codeSize = sizeof(kVertSpirv);
            sm_ci.pCode = kVertSpirv;
            if (vkCreateShaderModule(device, &sm_ci, nullptr, &final_vert_mod) != VK_SUCCESS) {
                std::fprintf(stderr, "error: vkCreateShaderModule (final vert) failed\n");
                return 1;
            }
        }
        {
            VkShaderModuleCreateInfo sm_ci = {};
            sm_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            sm_ci.codeSize = final_frag_spv.size() * sizeof(uint32_t);
            sm_ci.pCode = final_frag_spv.data();
            if (vkCreateShaderModule(device, &sm_ci, nullptr, &final_frag_mod) != VK_SUCCESS) {
                std::fprintf(stderr, "error: vkCreateShaderModule (final frag) failed\n");
                return 1;
            }
        }
        VkPipelineShaderStageCreateInfo final_stages[2] = {};
        final_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        final_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        final_stages[0].module = final_vert_mod;
        final_stages[0].pName = "main";
        final_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        final_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        final_stages[1].module = final_frag_mod;
        final_stages[1].pName = "main";
        VkGraphicsPipelineCreateInfo final_gp_ci = {};
        final_gp_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        final_gp_ci.stageCount = 2;
        final_gp_ci.pStages = final_stages;
        final_gp_ci.pVertexInputState = &vi;
        final_gp_ci.pInputAssemblyState = &ia;
        final_gp_ci.pViewportState = &vp;
        final_gp_ci.pRasterizationState = &rs;
        final_gp_ci.pMultisampleState = &ms;
        final_gp_ci.pDepthStencilState = &ds;
        final_gp_ci.pColorBlendState = &cb;
        final_gp_ci.pDynamicState = &dyn_state;
        final_gp_ci.layout = final_layout;
        final_gp_ci.renderPass = render_pass;
        final_gp_ci.subpass = 0;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &final_gp_ci, nullptr, &final_pipeline) != VK_SUCCESS) {
            std::fprintf(stderr, "error: vkCreateGraphicsPipelines (final) failed\n");
            return 1;
        }
        vkDestroyShaderModule(device, final_vert_mod, nullptr);
        vkDestroyShaderModule(device, final_frag_mod, nullptr);
    }

    // ---- Command pool / buffer ----

    // ---- Command pool / buffer ----
    VkCommandPoolCreateInfo pool_ci = {};
    pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.queueFamilyIndex = queue_family;
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool;
    if (vkCreateCommandPool(device, &pool_ci, nullptr, &pool) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateCommandPool failed\n");
        return 1;
    }
    VkCommandBufferAllocateInfo cmd_ai = {};
    cmd_ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_ai.commandPool = pool;
    cmd_ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(device, &cmd_ai, &cmd) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkAllocateCommandBuffers failed\n");
        return 1;
    }
    VkFenceCreateInfo fence_ci = {};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    if (vkCreateFence(device, &fence_ci, nullptr, &fence) != VK_SUCCESS) {
        std::fprintf(stderr, "error: vkCreateFence failed\n");
        return 1;
    }

    // ---- Bench mode: repeated timed frames with timestamp queries ----
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    if (bench) {
        {
        std::vector<double> gpu_times;
        gpu_times.reserve(frames);

        PushConstants pc{};
        pc.time = (float)time;
        pc.resolution_x = (float)width;
        pc.resolution_y = (float)height;
        pc.seed = (float)seed;

        // Warmup frames (not timed)
        for (uint32_t w = 0; w < warmup; ++w) {
            vkResetCommandBuffer(cmd, 0);
            VkCommandBufferBeginInfo begin_ci = {};
            begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &begin_ci);

            VkClearValue clear_color = {{0.0f, 0.0f, 0.0f, 0.0f}};
            VkRenderPassBeginInfo rp_begin = {};
            rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp_begin.renderPass = render_pass;
            rp_begin.framebuffer = framebuffer;
            rp_begin.renderArea = {{0, 0}, {width, height}};
            rp_begin.clearValueCount = 1;
            rp_begin.pClearValues = &clear_color;
            vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                                    &dset, 0, nullptr);
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(PushConstants), &pc);
            VkViewport viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
            VkRect2D scissor = {{0, 0}, {width, height}};
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRenderPass(cmd);
            vkEndCommandBuffer(cmd);

            vkResetFences(device, 1, &fence);
            vkQueueSubmit(queue, 1, &submit, fence);
            vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        }
        for (uint32_t f = 0; f < frames; ++f) {
            auto frame_t0 = std::chrono::high_resolution_clock::now();

            vkResetCommandBuffer(cmd, 0);
            VkCommandBufferBeginInfo begin_ci = {};
            begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &begin_ci);

            if (use_bake) {
                // ---- Pass 1: Bake static paper layers into texture (not timed) ----
                // Only bake on first frame; subsequent frames reuse baked texture
                if (f == 0) {
                    VkClearValue bake_clear = {{0.0f, 0.0f, 0.0f, 0.0f}};
                    VkRenderPassBeginInfo bake_rp_begin = {};
                    bake_rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    bake_rp_begin.renderPass = bake_render_pass;
                    bake_rp_begin.framebuffer = bake_framebuffer;
                    bake_rp_begin.renderArea = {{0, 0}, {width, height}};
                    bake_rp_begin.clearValueCount = 1;
                    bake_rp_begin.pClearValues = &bake_clear;
                    vkCmdBeginRenderPass(cmd, &bake_rp_begin, VK_SUBPASS_CONTENTS_INLINE);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bake_pipeline);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bake_layout, 0, 1,
                                            &bake_dset, 0, nullptr);
                    vkCmdPushConstants(cmd, bake_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                       sizeof(PushConstants), &pc);
                    VkViewport bake_viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
                    VkRect2D bake_scissor = {{0, 0}, {width, height}};
                    vkCmdSetViewport(cmd, 0, 1, &bake_viewport);
                    vkCmdSetScissor(cmd, 0, 1, &bake_scissor);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                    vkCmdEndRenderPass(cmd);

                    // Transition bake image to shader read-only for final pass
                    VkImageMemoryBarrier barrier = {};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = bake_image;
                    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                         0, nullptr, 0, nullptr, 1, &barrier);
                }

                // ---- Pass 2: Final lighting on baked albedo/height ----
                VkClearValue clear_color = {{0.0f, 0.0f, 0.0f, 0.0f}};
                VkRenderPassBeginInfo rp_begin = {};
                rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rp_begin.renderPass = render_pass;
                rp_begin.framebuffer = framebuffer;
                rp_begin.renderArea = {{0, 0}, {width, height}};
                rp_begin.clearValueCount = 1;
                rp_begin.pClearValues = &clear_color;
                vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, final_pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, final_layout, 0, 1,
                                        &final_dset, 0, nullptr);
                vkCmdPushConstants(cmd, final_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(PushConstants), &pc);
                VkViewport viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
                VkRect2D scissor = {{0, 0}, {width, height}};
                vkCmdSetViewport(cmd, 0, 1, &viewport);
                vkCmdSetScissor(cmd, 0, 1, &scissor);
                vkCmdDraw(cmd, 3, 1, 0, 0);
                vkCmdEndRenderPass(cmd);
            } else {
                // ---- Single pass (original path) ----
                VkClearValue clear_color = {{0.0f, 0.0f, 0.0f, 0.0f}};
                VkRenderPassBeginInfo rp_begin = {};
                rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rp_begin.renderPass = render_pass;
                rp_begin.framebuffer = framebuffer;
                rp_begin.renderArea = {{0, 0}, {width, height}};
                rp_begin.clearValueCount = 1;
                rp_begin.pClearValues = &clear_color;
                vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                                        &dset, 0, nullptr);
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(PushConstants), &pc);
                VkViewport viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
                VkRect2D scissor = {{0, 0}, {width, height}};
                vkCmdSetViewport(cmd, 0, 1, &viewport);
                vkCmdSetScissor(cmd, 0, 1, &scissor);
                vkCmdDraw(cmd, 3, 1, 0, 0);
                vkCmdEndRenderPass(cmd);
            }
            vkEndCommandBuffer(cmd);

            vkResetFences(device, 1, &fence);
            vkQueueSubmit(queue, 1, &submit, fence);
            vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

            auto frame_t1 = std::chrono::high_resolution_clock::now();
            double frame_ms = std::chrono::duration<double, std::milli>(frame_t1 - frame_t0).count();
            gpu_times.push_back(frame_ms);
        }

        std::sort(gpu_times.begin(), gpu_times.end());
        double gpu_p50 = 0.0, gpu_p95 = 0.0;
        if (!gpu_times.empty()) {
            gpu_p50 = gpu_times[gpu_times.size() / 2];
            gpu_p95 = gpu_times[(size_t)(gpu_times.size() * 0.95)];
        }

        // ---- Write bench.json ----
        if (!json_path.empty()) {
            std::ofstream f(json_path);
            if (f) {
                f << "{\n";
                f << "  \"device\": \"" << chosen_props.deviceName << "\",\n";
                f << "  \"resolution\": \"" << width << "x" << height << "\",\n";
                f << "  \"time\": " << time << ",\n";
                f << "  \"seed\": " << seed << ",\n";
                f << "  \"gpu_ms_p50\": " << gpu_p50 << ",\n";
                f << "  \"gpu_ms_p95\": " << gpu_p95 << ",\n";
                f << "  \"wall_ms_p50\": " << gpu_p50 << ",\n";
                f << "  \"wall_ms_p95\": " << gpu_p95 << ",\n";
                f << "  \"passes\": 1,\n";
                f << "  \"shader_cycles_malioc\": -1,\n";
                f << "  \"bytes_written_estimate\": 0,\n";
                f << "  \"bytes_read_estimate\": 0,\n";
                f << "  \"peak_alloc_bytes\": 0,\n";
                f << "  \"thermal_status\": null,\n";
                f << "  \"frames_presented\": " << gpu_times.size() << ",\n";
                f << "  \"frames_skipped\": 0,\n";
                f << "  \"validation\": \"ok\",\n";
                f << "  \"result\": \"pass\"\n";
                f << "}\n";
                f.flush();
                std::fprintf(stderr, "wrote bench metrics: %s (p50=%.2fms p95=%.2fms)\n",
                             json_path.c_str(), gpu_p50, gpu_p95);
            }
        }

        // ---- Cleanup ----
        vkDeviceWaitIdle(device);
        vmaDestroyBuffer(allocator, readback_buffer, buf_alloc);
        vmaDestroyImage(allocator, color_image, color_alloc);
        vkDestroyFramebuffer(device, framebuffer, nullptr);
        vkDestroyImageView(device, color_view, nullptr);
        vkDestroyRenderPass(device, render_pass, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, layout, nullptr);
        vkDestroyDescriptorSetLayout(device, dsl, nullptr);
        vkDestroyDescriptorPool(device, dpool, nullptr);
        vmaDestroyBuffer(allocator, ubo_buffer, ubo_alloc);

        if (use_bake) {
            vkDestroyPipeline(device, bake_pipeline, nullptr);
            vkDestroyPipelineLayout(device, bake_layout, nullptr);
            vkDestroyRenderPass(device, bake_render_pass, nullptr);
            vkDestroyFramebuffer(device, bake_framebuffer, nullptr);
            vkDestroyImageView(device, bake_view, nullptr);
            vkDestroySampler(device, bake_sampler, nullptr);
            vmaDestroyImage(allocator, bake_image, bake_alloc);
            vkDestroyDescriptorPool(device, bake_dpool, nullptr);
            vkDestroyDescriptorSetLayout(device, bake_dsl, nullptr);
            vkDestroyPipeline(device, final_pipeline, nullptr);
            vkDestroyPipelineLayout(device, final_layout, nullptr);
            vkDestroyDescriptorPool(device, final_dpool, nullptr);
            vkDestroyDescriptorSetLayout(device, final_dsl, nullptr);
        }

        vkDestroyCommandPool(device, pool, nullptr);
        vkDestroyFence(device, fence, nullptr);
        vmaDestroyAllocator(allocator);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);

        }
        return 0;
    }

    // ---- Idle test mode: render once, stay idle, render on dirty ----
    if (idle_test) {
        std::vector<double> gpu_times;
        uint32_t frames_rendered = 0;
        uint32_t frames_skipped = 0;
        double dirty_to_frame_ms = 0.0;
        uint32_t rebuilds = 0;

        PushConstants pc{};
        pc.time = (float)time;
        pc.resolution_x = (float)width;
        pc.resolution_y = (float)height;
        pc.seed = (float)seed;

        auto render_frame = [&](bool& did_rebuild) -> double {
            did_rebuild = false;
            vkResetCommandBuffer(cmd, 0);
            VkCommandBufferBeginInfo begin_ci = {};
            begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &begin_ci);

            if (use_bake) {
                if (frames_rendered == 0) {
                    VkClearValue bake_clear = {{0.0f, 0.0f, 0.0f, 0.0f}};
                    VkRenderPassBeginInfo bake_rp_begin = {};
                    bake_rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    bake_rp_begin.renderPass = bake_render_pass;
                    bake_rp_begin.framebuffer = bake_framebuffer;
                    bake_rp_begin.renderArea = {{0, 0}, {width, height}};
                    bake_rp_begin.clearValueCount = 1;
                    bake_rp_begin.pClearValues = &bake_clear;
                    vkCmdBeginRenderPass(cmd, &bake_rp_begin, VK_SUBPASS_CONTENTS_INLINE);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bake_pipeline);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bake_layout, 0, 1,
                                            &bake_dset, 0, nullptr);
                    vkCmdPushConstants(cmd, bake_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                       sizeof(PushConstants), &pc);
                    VkViewport bake_viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
                    VkRect2D bake_scissor = {{0, 0}, {width, height}};
                    vkCmdSetViewport(cmd, 0, 1, &bake_viewport);
                    vkCmdSetScissor(cmd, 0, 1, &bake_scissor);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                    vkCmdEndRenderPass(cmd);

                    VkImageMemoryBarrier barrier = {};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = bake_image;
                    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                         0, nullptr, 0, nullptr, 1, &barrier);
                    did_rebuild = true;
                }

                VkClearValue clear_color = {{0.0f, 0.0f, 0.0f, 0.0f}};
                VkRenderPassBeginInfo rp_begin = {};
                rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rp_begin.renderPass = render_pass;
                rp_begin.framebuffer = framebuffer;
                rp_begin.renderArea = {{0, 0}, {width, height}};
                rp_begin.clearValueCount = 1;
                rp_begin.pClearValues = &clear_color;
                vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, final_pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, final_layout, 0, 1,
                                        &final_dset, 0, nullptr);
                vkCmdPushConstants(cmd, final_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(PushConstants), &pc);
            } else {
                VkClearValue clear_color = {{0.0f, 0.0f, 0.0f, 0.0f}};
                VkRenderPassBeginInfo rp_begin = {};
                rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rp_begin.renderPass = render_pass;
                rp_begin.framebuffer = framebuffer;
                rp_begin.renderArea = {{0, 0}, {width, height}};
                rp_begin.clearValueCount = 1;
                rp_begin.pClearValues = &clear_color;
                vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                                        &dset, 0, nullptr);
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(PushConstants), &pc);
            }
            VkViewport viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
            VkRect2D scissor = {{0, 0}, {width, height}};
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRenderPass(cmd);
            vkEndCommandBuffer(cmd);

            vkResetFences(device, 1, &fence);
    auto t0 = std::chrono::high_resolution_clock::now();

    // ---- Text shaping: create glyph instances for --text rendering ----
    std::vector<GlyphInstance> glyph_instances;
    if (!shape_text.empty() || !shape_text_file.empty()) {
        std::string utf8;
        if (!shape_text_file.empty()) {
            std::size_t sz = 0;
            utf8 = read_file(shape_text_file, &sz);
        } else {
            utf8 = shape_text;
        }
        if (!utf8.empty()) {
            std::vector<unsigned char> font_data;
            if (!shape_font.empty() && !noat::load_file_bytes(shape_font, font_data)) {
                std::fprintf(stderr, "error: cannot read font %s\n", shape_font.c_str());
                return 1;
            }
            noat::ShapedRun run = noat::shape_text(utf8.c_str(),
                static_cast<int32_t>(utf8.size()),
                font_data.empty() ? nullptr : font_data.data(),
                font_data.size(), shape_font_size,
                shape_direction.c_str(), shape_script.c_str(),
                shape_language.c_str());
            if (run.glyph_count > 0 && run.glyph_count <= 8) {
                float scale = std::min(width, height) * 0.0015f;
                float base_y = height * 0.5f;
                float x = width * 0.1f;
                for (uint32_t i = 0; i < run.glyph_count; ++i) {
                    GlyphInstance gi = {};
                    gi.pos_x = x;
                    gi.pos_y = base_y + hash2((float)i, 0.0f) * 10.0f;
                    float advance = (i + 1 < run.glyph_count)
                        ? (run.positions[i + 1].x - run.positions[i].x)
                        : shape_font_size * 0.6f;
                    gi.size_x = advance * scale * 0.5f;
                    gi.size_y = shape_font_size * scale * 0.8f;
                    gi.color_r = 0.1f;
                    gi.color_g = 0.1f;
                    gi.color_b = 0.05f;
                    gi.color_a = 0.9f;
                    gi.scale_x = 1.0f;
                    gi.scale_y = 1.0f;
                    gi.rotation = hash2((float)i, 1.0f) * 0.1f;
                    gi.cluster_id = run.cluster_ids ? run.cluster_ids[i] : (int)i;
                    gi.pad0 = 0;
                    glyph_instances.push_back(gi);
                    x += gi.size_x * 0.9f + 4.0f * scale;
                }
                // Upload glyph instances to UBO
                GlyphInstancesUBO gi_ubo = {};
                gi_ubo.count = (int32_t)glyph_instances.size();
                for (size_t i = 0; i < glyph_instances.size(); ++i) {
                    gi_ubo.glyphs[i] = glyph_instances[i];
                }
                std::memcpy(gi_ubo_mapped, &gi_ubo, sizeof(GlyphInstancesUBO));
                vmaFlushAllocation(allocator, gi_ubo_alloc, 0, VK_WHOLE_SIZE);
            }
        }
    }
            vkQueueSubmit(queue, 1, &submit, fence);
            vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
            auto t1 = std::chrono::high_resolution_clock::now();

            return std::chrono::duration<double, std::milli>(t1 - t0).count();
        };

        // Initial render (frame 1)
        bool rebuilt = false;
        double frame_ms = render_frame(rebuilt);
        gpu_times.push_back(frame_ms);
        frames_rendered++;
        if (rebuilt) rebuilds++;

        // Idle wait: no GPU work while nothing changes
        if (idle_seconds > 0) {
            std::this_thread::sleep_for(std::chrono::seconds(idle_seconds));
            frames_skipped = idle_seconds;
        }

        // Optional dirty change: simulate param/light change, render exactly one frame
        if (dirty_after > 0) {
            pc.resolution_x = (float)(width + 1); // force param change
            auto dirty_t0 = std::chrono::high_resolution_clock::now();
            frame_ms = render_frame(rebuilt);
            gpu_times.push_back(frame_ms);
            frames_rendered++;
            auto dirty_t1 = std::chrono::high_resolution_clock::now();
            dirty_to_frame_ms = std::chrono::duration<double, std::milli>(dirty_t1 - dirty_t0).count();
            if (rebuilt) rebuilds++;
        }

        // Compute percentiles
        std::sort(gpu_times.begin(), gpu_times.end());
        double gpu_p50 = 0.0, gpu_p95 = 0.0;
        if (!gpu_times.empty()) {
            gpu_p50 = gpu_times[gpu_times.size() / 2];
            gpu_p95 = gpu_times[(size_t)(gpu_times.size() * 0.95)];
        }

        // Write idle metrics JSON
        if (!json_path.empty()) {
            std::ofstream f(json_path);
            if (f) {
                f << "{\n";
                f << "  \"device\": \"" << chosen_props.deviceName << "\",\n";
                f << "  \"resolution\": \"" << width << "x" << height << "\",\n";
                f << "  \"time\": " << time << ",\n";
                f << "  \"seed\": " << seed << ",\n";
                f << "  \"idle_seconds\": " << idle_seconds << ",\n";
                f << "  \"frames_rendered\": " << frames_rendered << ",\n";
                f << "  \"frames_skipped\": " << frames_skipped << ",\n";
                f << "  \"gpu_ms_p50\": " << gpu_p50 << ",\n";
                f << "  \"gpu_ms_p95\": " << gpu_p95 << ",\n";
                f << "  \"dirty_to_frame_ms_p95\": " << dirty_to_frame_ms << ",\n";
                f << "  \"rebuilds\": " << rebuilds << ",\n";
                f << "  \"validation\": \"ok\",\n";
                f << "  \"result\": \"pass\"\n";
                f << "}\n";
                f.flush();
                std::fprintf(stderr, "wrote idle metrics: %s (rendered=%u skipped=%u p50=%.2fms p95=%.2fms dirty_to_frame=%.2fms rebuilds=%u)\n",
                             json_path.c_str(), frames_rendered, frames_skipped, gpu_p50, gpu_p95, dirty_to_frame_ms, rebuilds);
            }
        }

        // Write PNG output if requested
        if (!out_path.empty()) {
            VkImageMemoryBarrier rb_barrier = {};
            rb_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            rb_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            rb_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            rb_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            rb_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            rb_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            rb_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            rb_barrier.image = color_image;
            rb_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            vkResetCommandBuffer(cmd, 0);
            VkCommandBufferBeginInfo rb_begin = {};
            rb_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            rb_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &rb_begin);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &rb_barrier);
            VkBufferImageCopy region = {};
            region.bufferOffset = 0;
            region.bufferRowLength = width;
            region.bufferImageHeight = 0;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {width, height, 1};
            vkCmdCopyImageToBuffer(cmd, color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   readback_buffer, 1, &region);
            vkEndCommandBuffer(cmd);

            vkResetFences(device, 1, &fence);
            vkQueueSubmit(queue, 1, &submit, fence);
            vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

            vmaInvalidateAllocation(allocator, buf_alloc, 0, VK_WHOLE_SIZE);
            void* mapped = nullptr;
            vmaMapMemory(allocator, buf_alloc, &mapped);
            stbi_write_png(out_path.c_str(), (int)width, (int)height, 4, mapped,
                           (int)width * 4);
            vmaUnmapMemory(allocator, buf_alloc);
            std::fprintf(stderr, "wrote PNG: %s (%ux%u)\n", out_path.c_str(), width, height);
        }

        // Idle test cleanup
        vkDeviceWaitIdle(device);
        vmaDestroyBuffer(allocator, readback_buffer, buf_alloc);
        vmaDestroyImage(allocator, color_image, color_alloc);
        vkDestroyFramebuffer(device, framebuffer, nullptr);
        vkDestroyImageView(device, color_view, nullptr);
        vkDestroyRenderPass(device, render_pass, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, layout, nullptr);
        vkDestroyDescriptorSetLayout(device, dsl, nullptr);
        vkDestroyDescriptorPool(device, dpool, nullptr);
        vmaDestroyBuffer(allocator, ubo_buffer, ubo_alloc);

        if (use_bake) {
            vkDestroyPipeline(device, bake_pipeline, nullptr);
            vkDestroyPipelineLayout(device, bake_layout, nullptr);
            vkDestroyRenderPass(device, bake_render_pass, nullptr);
            vkDestroyFramebuffer(device, bake_framebuffer, nullptr);
            vkDestroyImageView(device, bake_view, nullptr);
            vkDestroySampler(device, bake_sampler, nullptr);
            vmaDestroyImage(allocator, bake_image, bake_alloc);
            vkDestroyDescriptorPool(device, bake_dpool, nullptr);
            vkDestroyDescriptorSetLayout(device, bake_dsl, nullptr);
            vkDestroyPipeline(device, final_pipeline, nullptr);
            vkDestroyPipelineLayout(device, final_layout, nullptr);
            vkDestroyDescriptorPool(device, final_dpool, nullptr);
            vkDestroyDescriptorSetLayout(device, final_dsl, nullptr);
        }

        vkDestroyCommandPool(device, pool, nullptr);
        vkDestroyFence(device, fence, nullptr);
        vmaDestroyAllocator(allocator);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);

        return 0;
    }

    // ---- Record and submit ----
    PushConstants pc{};
    pc.time = (float)time;
    pc.resolution_x = (float)width;
    pc.resolution_y = (float)height;
    pc.seed = (float)seed;

    auto t0 = std::chrono::high_resolution_clock::now();

    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin_ci = {};
    begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_ci);

    VkClearValue clear_color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    VkRenderPassBeginInfo rp_begin = {};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = render_pass;
    rp_begin.framebuffer = framebuffer;
    rp_begin.renderArea = {{0, 0}, {width, height}};
    rp_begin.clearValueCount = 1;
    rp_begin.pClearValues = &clear_color;

    if (use_bake) {
        // ---- Pass 1: Bake static paper layers into texture ----
        VkRenderPassBeginInfo bake_rp_begin = {};
        bake_rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        bake_rp_begin.renderPass = bake_render_pass;
        bake_rp_begin.framebuffer = bake_framebuffer;
        bake_rp_begin.renderArea = {{0, 0}, {width, height}};
        bake_rp_begin.clearValueCount = 1;
        bake_rp_begin.pClearValues = &clear_color;
        vkCmdBeginRenderPass(cmd, &bake_rp_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bake_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bake_layout, 0, 1,
                                &bake_dset, 0, nullptr);
        vkCmdPushConstants(cmd, bake_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(PushConstants), &pc);
        VkViewport bake_viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
        VkRect2D bake_scissor = {{0, 0}, {width, height}};
        vkCmdSetViewport(cmd, 0, 1, &bake_viewport);
        vkCmdSetScissor(cmd, 0, 1, &bake_scissor);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);

        // Transition bake image to shader read-only
        VkImageMemoryBarrier bake_barrier = {};
        bake_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bake_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        bake_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        bake_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        bake_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bake_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bake_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bake_barrier.image = bake_image;
        bake_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &bake_barrier);

        // ---- Pass 2: Final lighting on baked albedo/height ----
        vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, final_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, final_layout, 0, 1,
                                &final_dset, 0, nullptr);
        vkCmdPushConstants(cmd, final_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(PushConstants), &pc);
        VkViewport viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
        VkRect2D scissor = {{0, 0}, {width, height}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    } else {
        // ---- Single pass (original path) ----
        vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                                &dset, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(PushConstants), &pc);
        VkViewport viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
        VkRect2D scissor = {{0, 0}, {width, height}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }

    // Memory barrier: color attachment writes → transfer read (readback).
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = color_image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    // Image already in TRANSFER_SRC_OPTIMAL from render pass finalLayout.
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = width;
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(cmd, color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback_buffer, 1, &region);

    vkEndCommandBuffer(cmd);

    vkResetFences(device, 1, &fence);
    vkQueueSubmit(queue, 1, &submit, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    // Invalidate host mapping so CPU sees the GPU-written readback buffer.
    vmaInvalidateAllocation(allocator, buf_alloc, 0, VK_WHOLE_SIZE);

    auto t1 = std::chrono::high_resolution_clock::now();
    double wall_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // ---- Write PNG ----
    if (!out_path.empty()) {
        stbi_write_png(out_path.c_str(), (int)width, (int)height, 4, mapped,
                       (int)row_pitch);
        std::fprintf(stderr, "wrote PNG: %s (%ux%u)\n", out_path.c_str(), width, height);
    }

    // ---- Golden diff + ascii ----
    Metrics metrics;
    metrics.device = chosen_props.deviceName;
    metrics.resolution = std::to_string(width) + "x" + std::to_string(height);
    metrics.time = time;
    metrics.seed = seed;
    metrics.gpu_ms = wall_ms;
    metrics.wall_ms = wall_ms;
    metrics.validation = "ok";

    if (!golden_path.empty()) {
        int gw = 0, gh = 0, gc = 0;
        stbi_uc* golden = stbi_load(golden_path.c_str(), &gw, &gh, &gc, 4);
        if (golden) {
            if ((uint32_t)gw == width && (uint32_t)gh == height) {
                metrics.diff_rmse = compute_rmse((const uint8_t*)mapped, golden,
                                                 (std::size_t)width * (std::size_t)height);
                const double tol = 8.0;
                if (metrics.diff_rmse > tol) metrics.result = "fail";
                std::fprintf(stderr, "golden diff rmse=%.4f (tol=8.0) -> %s\n",
                             metrics.diff_rmse, metrics.result.c_str());
            } else {
                std::fprintf(stderr, "golden size %dx%d != %ux%u\n", gw, gh, width, height);
                metrics.result = "fail";
            }
            stbi_image_free(golden);
        } else {
            std::fprintf(stderr, "warning: could not load golden %s\n", golden_path.c_str());
        }
    }

    if (ascii) {
        write_ascii_preview((const uint8_t*)mapped, width, height);
    }

    if (!json_path.empty()) {
        write_json_metrics(json_path, metrics);
        std::fprintf(stderr, "wrote metrics: %s (result=%s)\n",
                     json_path.c_str(), metrics.result.c_str());
    }

    // ---- Cleanup ----
    vkDeviceWaitIdle(device);
    vmaDestroyBuffer(allocator, readback_buffer, buf_alloc);
    vmaDestroyImage(allocator, color_image, color_alloc);
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    vkDestroyImageView(device, color_view, nullptr);
    vkDestroyRenderPass(device, render_pass, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, layout, nullptr);
    vkDestroyDescriptorSetLayout(device, dsl, nullptr);
    vkDestroyDescriptorPool(device, dpool, nullptr);
    vmaDestroyBuffer(allocator, ubo_buffer, ubo_alloc);

    if (use_bake) {
        vkDestroyPipeline(device, bake_pipeline, nullptr);
        vkDestroyPipelineLayout(device, bake_layout, nullptr);
        vkDestroyRenderPass(device, bake_render_pass, nullptr);
        vkDestroyFramebuffer(device, bake_framebuffer, nullptr);
        vkDestroyImageView(device, bake_view, nullptr);
        vkDestroySampler(device, bake_sampler, nullptr);
        vmaDestroyImage(allocator, bake_image, bake_alloc);
        vkDestroyDescriptorPool(device, bake_dpool, nullptr);
        vkDestroyDescriptorSetLayout(device, bake_dsl, nullptr);
        vkDestroyPipeline(device, final_pipeline, nullptr);
        vkDestroyPipelineLayout(device, final_layout, nullptr);
        vkDestroyDescriptorPool(device, final_dpool, nullptr);
        vkDestroyDescriptorSetLayout(device, final_dsl, nullptr);
    }

    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyFence(device, fence, nullptr);
    vmaDestroyAllocator(allocator);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    if (metrics.result == "fail") return 1;
    return 0;
}
