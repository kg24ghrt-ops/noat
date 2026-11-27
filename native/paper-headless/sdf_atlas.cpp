// sdf_atlas.cpp - Self-contained TrueType -> SDF glyph atlas.
// No external dependencies. Parses 'head/maxp/glyf/loca/hmtx/hhea', rasterizes
// simple and composite glyphs (nonzero fill) at a supersampled resolution,
// computes a signed distance field, and packs glyphs into an RGBA8 atlas
// (R = SDF, 128 = surface, <128 outside, >128 inside).
// Deterministic: same font + glyph ids + ppem => identical atlas.

#include "sdf_atlas.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace noat {

namespace {

// ---------- big-endian helpers ----------
uint16_t rd_u16(const unsigned char* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
int16_t rd_i16(const unsigned char* p) {
    return (int16_t)rd_u16(p);
}
uint32_t rd_u32(const unsigned char* p) {
    return (uint32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

// ---------- sfnt table directory ----------
struct TableRef {
    const unsigned char* data = nullptr;
    uint32_t length = 0;
    bool found = false;
};

static TableRef find_table(const unsigned char* font_data, size_t font_size,
                           uint32_t tag) {
    TableRef empty;
    if (font_size < 12) return empty;
    uint16_t num_tables = rd_u16(font_data + 4);
    size_t off = 12;
    for (uint16_t i = 0; i < num_tables; ++i) {
        if (off + 16 > font_size) return empty;
        uint32_t ttag = rd_u32(font_data + off);
        uint32_t toff = rd_u32(font_data + off + 8);
        uint32_t tlen = rd_u32(font_data + off + 12);
        if (ttag == tag) {
            TableRef r;
            r.data = font_data + toff;
            r.length = tlen;
            r.found = (toff + tlen <= font_size);
            return r;
        }
        off += 16;
    }
    return empty;
}

static const char* TAG_HEAD = "head";
static const char* TAG_MAXP = "maxp";
static const char* TAG_GLYF = "glyf";
static const char* TAG_LOCA = "loca";
static const char* TAG_HMTX = "hmtx";
static const char* TAG_HHEA = "hhea";

// fourcc
static uint32_t mk_tag(const char* s) {
    return (uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 |
           (uint32_t)s[3];
}

// ---------- 1D squared Euclidean distance transform (Felzenszwalb) ----------
// f: in/out. f[i] == 0 marks a site; non-sites start as a large value.
// Output: f[i] = squared distance to nearest site.
static void dt1d(float* f, int n) {
    const float INF = std::numeric_limits<float>::infinity();
    std::vector<int> v(n);
    std::vector<float> z(n + 1);
    int k = 0;
    v[0] = 0;
    z[0] = -INF;
    z[1] = INF;
    for (int q = 1; q < n; ++q) {
        while (true) {
            int vk = v[k];
            float s = ((f[q] + (float)q * q) - (f[vk] + (float)vk * vk)) /
                      (2.0f * q - 2.0f * vk);
            if (s <= z[k]) {
                if (k > 0) --k;
                else break;
            } else {
                break;
            }
        }
        int vk = v[k];
        float s = ((f[q] + (float)q * q) - (f[vk] + (float)vk * vk)) /
                  (2.0f * q - 2.0f * vk);
        ++k;
        v[k] = q;
        z[k] = s;
        z[k + 1] = INF;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < q) ++k;
        int d = q - v[k];
        f[q] = f[v[k]] + (float)(d * d);
    }
}

// squared Euclidean distance transform over a 2D grid (row-major, w x h).
// input: f[i] == 0 at sites, large elsewhere. output: squared distances.
static void dt2d(float* f, int w, int h) {
    // rows
    for (int y = 0; y < h; ++y) {
        dt1d(f + y * w, w);
    }
    // columns
    std::vector<float> col(std::max(w, h));
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) col[y] = f[y * w + x];
        dt1d(col.data(), h);
        for (int y = 0; y < h; ++y) f[y * w + x] = col[y];
    }
}

// ---------- outline point ----------
struct Pt {
    double x, y;
    bool on_curve;
};

struct Contour {
    std::vector<Pt> pts;
};

// Per-glyph rasterized data, independent of the atlas.
struct GlyphData {
    uint32_t id = 0;
    int32_t advance_width = 0;
    int32_t left_side_bearing = 0;
    int32_t xMin = 0, yMin = 0, xMax = 0, yMax = 0;
    int w = 0, h = 0;
    std::vector<uint8_t> mask; // supersample binary mask
};

// ---------- flatten quadratic bezier into line segments ----------
static void flatten_quad(double x1, double y1, double cx, double cy,
                         double x2, double y2, double tol,
                         std::vector<Pt>& out) {
    // Recursive subdivision until control point is close to the chord.
    double dx = x2 - x1, dy = y2 - y1;
    double d = std::fabs((cx - x1) * dy - (cy - y1) * dx);
    double len2 = dx * dx + dy * dy;
    if (len2 == 0.0) {
        out.push_back({x2, y2, true});
        return;
    }
    // distance of control point from chord, relative
    if (d * d <= tol * tol * len2 * 0.25) {
        out.push_back({x2, y2, true});
        return;
    }
    double mx = (x1 + cx) * 0.5, my = (y1 + cy) * 0.5;
    double m2x = (cx + x2) * 0.5, m2y = (cy + y2) * 0.5;
    double mmx = (mx + m2x) * 0.5, mmy = (my + m2y) * 0.5;
    flatten_quad(x1, y1, mx, my, mmx, mmy, tol, out);
    flatten_quad(mmx, mmy, m2x, m2y, x2, y2, tol, out);
}

// ---------- parse a glyph outline (simple) ----------
// Returns contours in image coords (y down). `advance`/`lsb` from hmtx.
static bool parse_simple_glyph(const unsigned char* glyph_data, uint32_t glyph_len,
                               double scale, double offset_x, double offset_y,
                               int32_t advance_width, int32_t left_side_bearing,
                               std::vector<Contour>& contours,
                               GlyphData& out) {
    if (glyph_len < 10) return false;
    const unsigned char* p = glyph_data;
    int16_t n_contours = rd_i16(p);
        if (n_contours < 0) return false; // composite handled elsewhere
    if (n_contours > 10000) return false; // sanity: absurd contour count
    p += 2;
    int16_t xMin = rd_i16(p); p += 2;
    int16_t yMin = rd_i16(p); p += 2;
    int16_t xMax = rd_i16(p); p += 2;
    int16_t yMax = rd_i16(p); p += 2;

    out.xMin = xMin; out.yMin = yMin; out.xMax = xMax; out.yMax = yMax;
    out.advance_width = advance_width;
    out.left_side_bearing = left_side_bearing;

    std::vector<int16_t> end_pts(n_contours);
    for (int i = 0; i < n_contours; ++i) {
        end_pts[i] = rd_i16(p); p += 2;
    }
    uint16_t instr_len = rd_u16(p); p += 2;
    p += instr_len; // skip instructions

    int total_points = end_pts.empty() ? 0 : end_pts.back() + 1;
    if (total_points > 100000) return false; // sanity: absurd point count
    std::vector<uint8_t> flags(total_points);
    std::vector<int16_t> x_coords(total_points), y_coords(total_points);

    // read flags with repeat
    int fi = 0;
    while (fi < total_points) {
        uint8_t f = *p++;
        flags[fi] = f;
        if (f & 0x08) { // repeat
            uint8_t rep = *p++;
            for (int r = 0; r < rep && fi + 1 + r < total_points; ++r) {
                flags[fi + 1 + r] = f;
            }
            fi += 1 + rep;
        } else {
            ++fi;
        }
    }
    // x coords
    int xi = 0;
    while (xi < total_points) {
        uint8_t f = flags[xi];
        if (f & 0x10) { // x same as previous
            int16_t v = (xi == 0) ? 0 : x_coords[xi - 1];
            x_coords[xi] = v;
        } else if (f & 0x02) { // x short (1 byte, positive)
            x_coords[xi] = (int16_t)*p++;
        } else { // x int16
            x_coords[xi] = rd_i16(p); p += 2;
        }
        ++xi;
    }
    // y coords
    int yi = 0;
    while (yi < total_points) {
        uint8_t f = flags[yi];
        if (f & 0x20) { // y same as previous
            int16_t v = (yi == 0) ? 0 : y_coords[yi - 1];
            y_coords[yi] = v;
        } else if (f & 0x04) { // y short (1 byte, positive)
            y_coords[yi] = (int16_t)*p++;
        } else { // y int16
            y_coords[yi] = rd_i16(p); p += 2;
        }
        ++yi;
    }

    // Convert delta-encoded coordinates to absolute, then to image coords.
    {
        int16_t acc_x = 0, acc_y = 0;
        for (int pi = 0; pi < total_points; ++pi) {
            acc_x += x_coords[pi];
            acc_y += y_coords[pi];
            x_coords[pi] = acc_x;
            y_coords[pi] = acc_y;
        }
    }
    auto to_img_x = [&](int16_t v) { return (double)v * scale + offset_x; };
    auto to_img_y = [&](int16_t v) { return offset_y - (double)v * scale; };

    int prev_end = -1;
    for (int ci = 0; ci < n_contours; ++ci) {
        int end = end_pts[ci];
        Contour c;
        c.pts.reserve(end - prev_end);
        for (int pi = prev_end + 1; pi <= end; ++pi) {
            double x = to_img_x(x_coords[pi]);
            double y = to_img_y(y_coords[pi]);
            c.pts.push_back({x, y, (flags[pi] & 0x01) != 0});
        }
        prev_end = end;
        contours.push_back(std::move(c));
    }
    return true;
}

// ---------- parse composite glyph ----------
// Recursively extracts contours of referenced glyphs with transform.
static bool parse_glyph_outline(const unsigned char* glyf_data,
                                const unsigned char* loca_data,
                                int loca_index_size, uint32_t num_glyphs,
                                uint32_t gid, double scale,
                                double offset_x, double offset_y,
                                std::vector<Contour>& contours,
                                GlyphData& out_metrics,
                                int depth = 0) {
    if (depth > 32) return false;
    if (gid >= num_glyphs) return false;
    uint32_t off0 = (loca_index_size == 2)
                        ? (uint32_t)rd_u16(loca_data + gid * 2)
                        : rd_u32(loca_data + gid * 4);
    uint32_t off1 = (loca_index_size == 2)
                        ? (uint32_t)rd_u16(loca_data + (gid + 1) * 2)
                        : rd_u32(loca_data + (gid + 1) * 4);
    if (off1 <= off0 || off1 - off0 > 1u << 20) return false; // sanity
    const unsigned char* g = glyf_data + off0;
    uint32_t glen = off1 - off0;
    if (glen < 10) return false;

    int16_t n_contours = rd_i16(g);
    g += 2;
    // skip bbox
    g += 8;

    if (n_contours >= 0) {
        // Need advance/lsb from hmtx — but this function doesn't have it.
        // For composite children we only need geometry; metrics handled at root.
        std::vector<Contour> child;
        // re-parse with a metrics-free path: reuse parse_simple_glyph needs hmtx.
        // Instead parse contours manually here for children (metrics unused).
        const unsigned char* p = g;
        // p currently points after bbox (10 bytes consumed from glyph start incl n_contours)
        // We already consumed n_contours(2)+bbox(8)=10, so p = glyph_start+10.
        std::vector<int16_t> end_pts(n_contours);
        for (int i = 0; i < n_contours; ++i) {
            end_pts[i] = rd_i16(p); p += 2;
        }
        uint16_t instr_len = rd_u16(p); p += 2;
        p += instr_len;
        int total_points = end_pts.empty() ? 0 : end_pts.back() + 1;
        std::vector<uint8_t> flags(total_points);
        std::vector<int16_t> x_coords(total_points), y_coords(total_points);
        int fi = 0;
        while (fi < total_points) {
            uint8_t f = *p++;
            flags[fi] = f;
            if (f & 0x08) {
                uint8_t rep = *p++;
                for (int r = 0; r < rep && fi + 1 + r < total_points; ++r)
                    flags[fi + 1 + r] = f;
                fi += 1 + rep;
            } else {
                ++fi;
            }
        }
        int xi = 0;
        while (xi < total_points) {
            uint8_t f = flags[xi];
            if (f & 0x10) x_coords[xi] = (xi == 0) ? 0 : x_coords[xi - 1];
            else if (f & 0x02) x_coords[xi] = (int16_t)*p++;
            else { x_coords[xi] = rd_i16(p); p += 2; }
            ++xi;
        }
        int yi = 0;
        while (yi < total_points) {
            uint8_t f = flags[yi];
            if (f & 0x20) y_coords[yi] = (yi == 0) ? 0 : y_coords[yi - 1];
            else if (f & 0x04) y_coords[yi] = (int16_t)*p++;
            else { y_coords[yi] = rd_i16(p); p += 2; }
            ++yi;
        }
        {
            int16_t acc_x = 0, acc_y = 0;
            for (int pi = 0; pi < total_points; ++pi) {
                acc_x += x_coords[pi];
                acc_y += y_coords[pi];
                x_coords[pi] = acc_x;
                y_coords[pi] = acc_y;
            }
        }
        auto to_img_x = [&](int16_t v) { return (double)v * scale + offset_x; };
        auto to_img_y = [&](int16_t v) { return offset_y - (double)v * scale; };
        int prev_end = -1;
        for (int ci = 0; ci < n_contours; ++ci) {
            int end = end_pts[ci];
            Contour c;
            c.pts.reserve(end - prev_end);
            for (int pi = prev_end + 1; pi <= end; ++pi) {
                double x = to_img_x(x_coords[pi]);
                double y = to_img_y(y_coords[pi]);
                c.pts.push_back({x, y, (flags[pi] & 0x01) != 0});
            }
            prev_end = end;
            contours.push_back(std::move(c));
        }
        return true;
    }

    // composite
    const unsigned char* p = g;
    while (true) {
        uint16_t flags16 = rd_u16(p); p += 2;
        uint32_t glyph_index = (flags16 & 0x0001) ? rd_u32(p) : rd_u16(p);
        p += (flags16 & 0x0001) ? 4 : 2;
        int32_t arg1, arg2;
        if (flags16 & 0x0001) { // ARG_1_AND_2_ARE_WORDS
            arg1 = rd_i16(p); p += 2;
            arg2 = rd_i16(p); p += 2;
        } else {
            arg1 = (int8_t)*p++;
            arg2 = (int8_t)*p++;
        }
        double xx = 1.0, xy = 0.0, yx = 0.0, yy = 1.0;
        if (flags16 & 0x0008) { // WE_HAVE_A_SCALE
            int16_t sc = rd_i16(p); p += 2;
            xx = yy = sc / 16384.0;
        } else if (flags16 & 0x0040) { // WE_HAVE_AN_X_AND_Y_SCALE
            int16_t scx = rd_i16(p); p += 2;
            int16_t scy = rd_i16(p); p += 2;
            xx = scx / 16384.0;
            yy = scy / 16384.0;
        } else if (flags16 & 0x0080) { // WE_HAVE_A_TWO_BY_TWO
            int16_t scx = rd_i16(p); p += 2;
            int16_t sc01 = rd_i16(p); p += 2;
            int16_t sc10 = rd_i16(p); p += 2;
            int16_t scy = rd_i16(p); p += 2;
            xx = scx / 16384.0; xy = sc01 / 16384.0;
            yx = sc10 / 16384.0; yy = scy / 16384.0;
        }
        bool args_are_xy = (flags16 & 0x0002) != 0;
        double dx = args_are_xy ? (double)arg1 * scale : 0.0;
        double dy = args_are_xy ? (double)arg2 * scale : 0.0;
        double cx = offset_x + dx;
        double cy = offset_y + dy;

        if (!args_are_xy) {
            // offset points: measured in the parent's coordinate system from the
            // parent glyph's origin; convert as a translation already applied.
            cx = offset_x + (double)arg1 * scale;
            cy = offset_y + (double)arg2 * scale;
        }

        // Apply component transform to child's offset and recurse.
        // Child local origin maps to (cx, cy); scale and 2x2 applied.
        double child_ox = cx;
        double child_oy = cy;
        // The transform maps child coords (u,v) -> (xx*u + yx*v + cx, xy*u + yy*v + cy)
        // To recurse we pass offset such that child point (u,v) in font units maps
        // to image: image = scale * M * (u,v) + (child_ox, child_oy) with y-flip handled.
        // Since our to_img already flips y and applies scale, we pre-compose by
        // scaling the offset and passing an adjusted offset. The 2x2 linear part
        // must be applied to child contours. Simplest: transform child contour points
        // after recursion via a post-transform. We handle by recursing with an offset
        // and then transforming the added contours linearly.
        size_t base = contours.size();
        if (!parse_glyph_outline(glyf_data, loca_data, loca_index_size, num_glyphs,
                                 glyph_index, scale, child_ox, child_oy, contours,
                                 out_metrics, depth + 1)) {
            return false;
        }
        // Apply linear transform + translation to newly added contours.
        if (xx != 1.0 || xy != 0.0 || yx != 0.0 || yy != 1.0) {
            for (size_t i = base; i < contours.size(); ++i) {
                for (auto& pt : contours[i].pts) {
                    double u = pt.x - child_ox;
                    double v = pt.y - child_oy;
                    pt.x = child_ox + xx * u + yx * v;
                    pt.y = child_oy + xy * u + yy * v;
                }
            }
        }
        if (!(flags16 & 0x0020)) break; // MORE_COMPONENTS
    }
    return true;
}

} // namespace

// ---------- rasterize contours to a supersampled binary mask ----------
struct Edge { double x1, y1, x2, y2; };
static inline void flatten_quad_push(double x1, double y1, double cx, double cy,
                                     double x2, double y2, double tol,
                                     std::vector<Edge>& out);

static void rasterize_contours(const std::vector<Contour>& contours,
                               int width, int height, std::vector<uint8_t>& mask) {
    mask.assign((size_t)width * (size_t)height, 0);
    if (contours.empty()) return;

    const double tol = 0.25; // flatten tolerance in pixels
    std::vector<Edge> edges;

    for (const auto& c : contours) {
        const auto& pts = c.pts;
        size_t n = pts.size();
        if (n < 2) continue;
        // Find first on-curve point to start.
        size_t start = 0;
        while (start < n && !pts[start].on_curve) ++start;
        if (start == n) continue; // all off-curve, skip contour
        size_t i = start;
        Pt cur = pts[i];
        int guard = 0;
        do {
            size_t next = (i + 1) % n;
            const Pt& nxt = pts[next];
            if (nxt.on_curve) {
                edges.push_back({cur.x, cur.y, nxt.x, nxt.y});
                cur = nxt;
                i = next;
            } else {
                // nxt is off-curve: quadratic cur -> nxt(control) -> end
                size_t end = (next + 1) % n;
                const Pt& end_pt = pts[end];
                if (end == start) {
                    // closing back to start: flatten cur -> nxt -> cur
                    flatten_quad_push(cur.x, cur.y, nxt.x, nxt.y, cur.x, cur.y,
                                      tol, edges);
                    break;
                }
                flatten_quad_push(cur.x, cur.y, nxt.x, nxt.y, end_pt.x, end_pt.y,
                                  tol, edges);
                cur = end_pt;
                i = end;
            }
            ++guard;
        } while (i != start && guard < (int)n * 4 + 8);
    }

    // Scanline fill (even-odd rule) at pixel centers.
    for (int ry = 0; ry < height; ++ry) {
        double y = (double)ry + 0.5;
        std::vector<double> xs;
        xs.reserve(edges.size());
        for (const auto& e : edges) {
            if ((e.y1 <= y) != (e.y2 <= y)) {
                double t = (y - e.y1) / (e.y2 - e.y1);
                xs.push_back(e.x1 + t * (e.x2 - e.x1));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            int x0 = (int)std::ceil(xs[k]);
            int x1 = (int)std::floor(xs[k + 1]);
            if (x0 < 0) x0 = 0;
            if (x1 >= width) x1 = width - 1;
            for (int x = x0; x <= x1; ++x) {
                mask[(size_t)ry * (size_t)width + (size_t)x] = 1;
            }
        }
    }
}

// Temporary used by quadratic flatten within rasterize (kept simple).
static inline void flatten_quad_push(double x1, double y1, double cx, double cy,
                                     double x2, double y2, double tol,
                                     std::vector<Edge>& out) {
    // iterative subdivision into line segments pushed to out
    std::vector<std::tuple<double,double,double,double,double,double,int>> stack;
    stack.emplace_back(x1, y1, cx, cy, x2, y2, 0);
    while (!stack.empty()) {
        auto [a, b, c, d, e, f, dep] = stack.back();
        stack.pop_back();
        double dx = e - a, dy = f - b;
        double dist = std::fabs((c - a) * dy - (d - b) * dx);
        double len2 = dx * dx + dy * dy;
        if (len2 == 0.0 || dep > 12 || dist * dist <= tol * tol * len2 * 0.25) {
            out.push_back({a, b, e, f});
        } else {
            double mx = (a + c) * 0.5, my = (b + d) * 0.5;
            double m2x = (c + e) * 0.5, m2y = (d + f) * 0.5;
            double mmx = (mx + m2x) * 0.5, mmy = (my + m2y) * 0.5;
            stack.emplace_back(mmx, mmy, m2x, m2y, e, f, dep + 1);
            stack.emplace_back(a, b, mx, my, mmx, mmy, dep + 1);
        }
    }
}

} // namespace

namespace noat {

SdfAtlasResult bake_sdf_atlas(const unsigned char* font_data, size_t font_size,
                              const std::vector<uint32_t>& glyph_ids,
                              float ppem, int32_t supersample,
                              int32_t font_units_per_em_override) {
    SdfAtlasResult result;
    result.ppem = ppem;
    if (glyph_ids.empty()) return result;

    TableRef head = find_table(font_data, font_size, mk_tag("head"));
    TableRef maxp = find_table(font_data, font_size, mk_tag("maxp"));
    TableRef glyf = find_table(font_data, font_size, mk_tag("glyf"));
    TableRef loca = find_table(font_data, font_size, mk_tag("loca"));
    TableRef hmtx = find_table(font_data, font_size, mk_tag("hmtx"));
    TableRef hhea = find_table(font_data, font_size, mk_tag("hhea"));
    if (!head.found || !maxp.found || !glyf.found || !loca.found ||
        !hmtx.found || !hhea.found) {
        return result;
    }

    uint16_t num_glyphs = rd_u16(maxp.data + 4);
    int16_t units_per_em = font_units_per_em_override > 0
                               ? (int16_t)font_units_per_em_override
                               : rd_i16(head.data + 18);
    if (units_per_em <= 0) units_per_em = 1000; // fallback for corrupt fonts
    int16_t index_to_loc_format = rd_i16(head.data + 50);
    int loca_index_size = (index_to_loc_format == 0) ? 2 : 4;
    uint16_t num_hmetrics = rd_u16(hhea.data + 34);

    double scale = (double)ppem * (double)supersample / (double)units_per_em;

    // Read advance + lsb for all glyphs.
    std::vector<int32_t> advance(num_glyphs, 0);
    std::vector<int32_t> lsb(num_glyphs, 0);
    {
        const unsigned char* p = hmtx.data;
        for (uint16_t i = 0; i < num_hmetrics && i < num_glyphs; ++i) {
            advance[i] = rd_u16(p); p += 2;
            lsb[i] = rd_i16(p); p += 2;
        }
        for (uint16_t i = num_hmetrics; i < num_glyphs; ++i) {
            lsb[i] = rd_i16(p); p += 2;
        }
    }

    // Rasterize each glyph at supersample resolution.
    std::vector<GlyphData> glyphs;
    glyphs.reserve(glyph_ids.size());

    int max_w = 0, max_h = 0;
    for (uint32_t gid : glyph_ids) {
        GlyphData gd;
        gd.id = gid;
        gd.advance_width = (gid < num_glyphs) ? advance[gid] : 0;
        gd.left_side_bearing = (gid < num_glyphs) ? lsb[gid] : 0;
        gd.w = 0;
        gd.h = 0;
        gd.xMin = gd.yMin = gd.xMax = gd.yMax = 0;

        try {
    uint32_t off0 = (loca_index_size == 2)
                        ? (uint32_t)rd_u16(loca_data + gid * 2) * 2
                        : rd_u32(loca_data + gid * 4);
    uint32_t off1 = (loca_index_size == 2)
                        ? (uint32_t)rd_u16(loca_data + (gid + 1) * 2) * 2
                        : rd_u32(loca_data + (gid + 1) * 4);
            if (off1 <= off0 || off1 - off0 > (1u << 20)) {
                // empty glyph
                glyphs.push_back(gd);
                continue;
            }
            const unsigned char* g = glyf.data + off0;
            int16_t n_contours = rd_i16(g);
            std::vector<Contour> contours;
            bool ok;
            if (n_contours >= 0) {
                ok = parse_simple_glyph(g, off1 - off0, scale, 0, 0,
                                        gd.advance_width, gd.left_side_bearing,
                                        contours, gd);
            } else {
                // composite: no hmtx here; get metrics from root parse separately.
                ok = parse_glyph_outline(glyf.data, loca.data, loca_index_size,
                                         num_glyphs, gid, scale, 0, 0, contours, gd, 0);
            }
            if (!ok) {
                gd.w = 0; gd.h = 0;
                glyphs.push_back(gd);
                continue;
            }
            // compute bounds
            double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
            for (auto& c : contours) {
                for (auto& pt : c.pts) {
                    minx = std::min(minx, pt.x);
                    miny = std::min(miny, pt.y);
                    maxx = std::max(maxx, pt.x);
                    maxy = std::max(maxy, pt.y);
                }
            }
            if (contours.empty()) {
                gd.w = 0; gd.h = 0;
                glyphs.push_back(gd);
                continue;
            }
            // image coords: x = (x - minx), y = (maxy - y) [y down]
            double ox = minx;
            double oy = maxy;
            // rebuild with offset applied: shift contours
            for (auto& c : contours) {
                for (auto& pt : c.pts) {
                    pt.x -= ox;
                    pt.y = oy - pt.y;
                }
            }
            int w = (int)std::ceil(maxx - minx) + 1;
            int h = (int)std::ceil(maxy - miny) + 1;
            if (w < 1) w = 1;
            if (h < 1) h = 1;
            // Reject absurdly large glyphs (corrupt data, composite transforms, etc.)
            const int kMaxGlyphDim = 4096;
            if (w > kMaxGlyphDim || h > kMaxGlyphDim) {
                gd.w = 0; gd.h = 0;
                glyphs.push_back(gd);
                continue;
            }
            // Reject absurd pixel counts (w*h overflow or too much memory)
            if ((size_t)w * (size_t)h > (16u * 1024u * 1024u)) {
                gd.w = 0; gd.h = 0;
                glyphs.push_back(gd);
                continue;
            }
            gd.w = w; gd.h = h;
            rasterize_contours(contours, w, h, gd.mask);
            max_w = std::max(max_w, w);
            max_h = std::max(max_h, h);
        } catch (...) {
            // Skip any glyph that throws during parsing/rasterization
            gd.w = 0; gd.h = 0;
        }
        glyphs.push_back(gd);
    }

    // ---------- pack atlas (shelf packing) ----------
    int pad = 2; // padding between glyphs
    int row_h = 0;
    int atlas_w = 0;
    int atlas_h = 0;
    int cur_x = 0, cur_y = 0;
    std::vector<int> glyph_atlas_x(glyphs.size(), 0);
    std::vector<int> glyph_atlas_y(glyphs.size(), 0);

    for (size_t i = 0; i < glyphs.size(); ++i) {
        int w = glyphs[i].w;
        int h = glyphs[i].h;
        if (w <= 0 || h <= 0) continue;
        if (cur_x + w + pad > atlas_w && cur_x != 0) {
            // new row
            cur_x = 0;
            cur_y += row_h + pad;
            row_h = 0;
        }
        if (cur_x + w > atlas_w) atlas_w = cur_x + w;
        row_h = std::max(row_h, h);
        glyph_atlas_x[i] = cur_x;
        glyph_atlas_y[i] = cur_y;
        cur_x += w + pad;
        atlas_h = std::max(atlas_h, cur_y + h);
    }
    atlas_w = std::max(atlas_w, 1);
    atlas_h = std::max(atlas_h, 1);

    // Cap atlas size; if too big, we still proceed (caller can split).
    result.atlas_width = atlas_w;
    result.atlas_height = atlas_h;
    result.scale = scale / (double)supersample; // final pixels per font unit
    result.atlas_rgba8.assign((size_t)atlas_w * (size_t)atlas_h * 4, 0);

    // ---------- SDF per glyph ----------
    for (size_t i = 0; i < glyphs.size(); ++i) {
        auto& gd = glyphs[i];
        if (gd.w <= 0 || gd.h <= 0) continue;
        int ax = glyph_atlas_x[i];
        int ay = glyph_atlas_y[i];
        int w = gd.w, h = gd.h;

        // signed distance on supersample grid: inside +, outside -
        const float INF = std::numeric_limits<float>::max();
        std::vector<float> dist((size_t)w * (size_t)h, INF);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (gd.mask[(size_t)y * w + (size_t)x]) dist[(size_t)y * w + (size_t)x] = 0.0f;
            }
        }
        // D_out: distance to nearest outside (for inside pixels)
        std::vector<float> d_out = dist;
        for (size_t k = 0; k < d_out.size(); ++k)
            if (gd.mask[k]) d_out[k] = 0.0f; else d_out[k] = INF; // sites = outside? 
        // We want for each inside pixel distance to nearest outside.
        // Build f: 0 at outside, INF at inside -> dt2d -> dist to nearest outside.
        std::vector<float> f_out((size_t)w * (size_t)h, INF);
        for (size_t k = 0; k < f_out.size(); ++k)
            f_out[k] = gd.mask[k] ? INF : 0.0f;
        dt2d(f_out.data(), w, h);
        // f_in: 0 at inside, INF at outside -> dist to nearest inside
        std::vector<float> f_in((size_t)w * (size_t)h, INF);
        for (size_t k = 0; k < f_in.size(); ++k)
            f_in[k] = gd.mask[k] ? 0.0f : INF;
        dt2d(f_in.data(), w, h);

        // convert distances from supersample pixels to final pixels
        double inv_ss = 1.0 / (double)supersample;
        // clamp max distance
        double max_d = (double)std::max(w, h) * inv_ss;

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                size_t k = (size_t)y * w + (size_t)x;
                bool inside = gd.mask[k] != 0;
                double d;
                if (inside) {
                    d = std::sqrt(f_out[k]) * inv_ss;
                    d = std::min(d, max_d);
                } else {
                    d = -std::sqrt(f_in[k]) * inv_ss;
                    d = std::max(d, -max_d);
                }
                // map to 0..255: 128 = surface (0), scale so max_d ~ 127
                double sdf_norm = d / max_d; // -1..1
                int v = (int)std::round(128.0 + sdf_norm * 127.0);
                if (v < 0) v = 0;
                if (v > 255) v = 255;
                int ai = ay + y;
                int aj = ax + x;
                if (ai >= 0 && ai < atlas_h && aj >= 0 && aj < atlas_w) {
                    size_t idx = ((size_t)ai * (size_t)atlas_w + (size_t)aj) * 4;
                    result.atlas_rgba8[idx] = (uint8_t)v; // R = SDF
                    result.atlas_rgba8[idx + 1] = 0;
                    result.atlas_rgba8[idx + 2] = 0;
                    result.atlas_rgba8[idx + 3] = 255;
                }
            }
        }

        AtlasGlyph ag;
        ag.glyph_id = gd.id;
        ag.atlas_x = ax;
        ag.atlas_y = ay;
        ag.atlas_w = w;
        ag.atlas_h = h;
        ag.offset_x = 0;   // bitmap origin = atlas rect origin
        ag.offset_y = 0;
        ag.advance_x = gd.advance_width * result.scale;
        ag.bearing_x = gd.left_side_bearing * result.scale;
        ag.xMin = 0;
        ag.yMin = 0;
        ag.xMax = w * result.scale;
        ag.yMax = h * result.scale;
        result.glyphs.push_back(ag);
    }

    return result;
} // close bake_sdf_atlas
} // namespace noat
