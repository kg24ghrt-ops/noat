// text_document.cpp - Grapheme-cluster-aware text document model.
// UTF-8 stored, cluster-aware cursor/backspace, multiple logical lines,
// positioned glyph output for GPU rendering.
//
// Grapheme cluster segmentation follows a simplified UAX #29 algorithm:
// - CR x LF: stay together
// - Control x any: break
// - Extend (combining marks) x base: stay together
// - ZWJ sequences: stay together
// - Myanmar marks (Mn/Mc) attach to base consonants/vowels.
//
#include "text_document.h"

#include <cstring>
#include <vector>

namespace noat {

// ---------- UTF-8 helpers ----------

// Decode one UTF-8 codepoint from p, advance p past it. Returns -1 on invalid.
static int32_t decode_utf8(const char** p, const char* end) {
    if (*p >= end) return -1;
    unsigned char c = static_cast<unsigned char>(**p);
    uint32_t codepoint = 0;
    int bytes = 0;

    if (c < 0x80) {
        codepoint = c;
        bytes = 1;
    } else if ((c & 0xE0) == 0xC0) {
        codepoint = c & 0x1F;
        bytes = 2;
    } else if ((c & 0xF0) == 0xE0) {
        codepoint = c & 0x0F;
        bytes = 3;
    } else if ((c & 0xF8) == 0xF0) {
        codepoint = c & 0x07;
        bytes = 4;
    } else {
        // Invalid lead byte, skip one.
        ++(*p);
        return -1;
    }

    if (static_cast<size_t>(end - *p) < bytes) return -1;

    for (int i = 1; i < bytes; ++i) {
        unsigned char b = static_cast<unsigned char>((*p)[i]);
        if ((b & 0xC0) != 0x80) return -1; // invalid continuation
        codepoint = (codepoint << 6) | (b & 0x3F);
    }

    *p += bytes;
    return static_cast<int32_t>(codepoint);
}

// Encode one codepoint to UTF-8, returns bytes written.
static int encode_utf8(int32_t cp, char* out) {
    if (cp < 0x80) {
        out[0] = static_cast<char>(cp);
        return 1;
    } else if (cp < 0x800) {
        out[0] = static_cast<char>(0xC0 | (cp >> 6));
        out[1] = static_cast<char>(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (cp >> 12));
        out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp < 0x110000) {
        out[0] = static_cast<char>(0xF0 | (cp >> 18));
        out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[3] = static_cast<char>(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

// ---------- Grapheme cluster categorization ----------

// General_Category values relevant to grapheme breaking.
enum class GC {
    Other,      // default
    CR,         // \r
    LF,         // \n
    Control,    // other Cc/Cf
    Extend,     // Mn, Me (combining marks)
    ZWJ,        // U+200D
    RegionalIndicator, // U+1F1E6..U+1F1FF
    Prepend,    // Cc that prepends
    SpacingMark, // Mc (Myanmar medial signs etc.)
};

static GC categorize(int32_t cp) {
    if (cp == 0x0D) return GC::CR;
    if (cp == 0x0A) return GC::LF;
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return GC::Control;
    if (cp == 0x200D) return GC::ZWJ;
    if (cp >= 0x1F1E6 && cp <= 0x1F1FF) return GC::RegionalIndicator;

    // Combining marks (Mn, Me) -> Extend
    if (cp >= 0x0300 && cp <= 0x036F) return GC::Extend; // Combining Diacritical Marks
    if (cp >= 0x1AB0 && cp <= 0x1ABE) return GC::Extend; // Combining Diacritical Marks Extended
    if (cp >= 0x1DC0 && cp <= 0x1DFF) return GC::Extend; // Combining Diacritical Marks Supplement
    if (cp >= 0x20D0 && cp <= 0x20FF) return GC::Extend; // Combining Diacritical Marks for Symbols
    if (cp >= 0xFE00 && cp <= 0xFE0F) return GC::Extend; // Variation Selectors
    if (cp >= 0xFE20 && cp <= 0xFE2F) return GC::Extend; // Combining Half Marks

    // Myanmar combining marks (U+1000..U+109F block):
    // Most are combining marks (Mn/Mc) that attach to base characters.
    if (cp >= 0x1000 && cp <= 0x109F) {
        // Myanmar vowel signs and marks that are combining:
        // U+102E..U+1032 (vowel signs, medials), U+1036..U+1039 (signs),
        // U+103A (asat), U+103D..U+103E (consonant signs),
        // U+1058..U+1059 (digits? no), U+105E..U+1060 (vowel signs),
        // U+1062..U+1064 (vowel signs), U+1067..U+106D (vowel signs),
        // U+1083..U+1084 (digits? no), U+1085..U+1086 (signs),
        // U+108E..U+108F (digits? no), U+109D..U+109F (signs)
        // Simplified: treat most non-digit, non-punctuation Myanmar as Extend/SpacingMark
        if (cp >= 0x102E && cp <= 0x1039) return GC::Extend;
        if (cp == 0x103A) return GC::Extend; // asat
        if (cp >= 0x103D && cp <= 0x103E) return GC::Extend;
        if (cp >= 0x105E && cp <= 0x1060) return GC::Extend;
        if (cp >= 0x1062 && cp <= 0x1064) return GC::Extend;
        if (cp >= 0x1067 && cp <= 0x106D) return GC::Extend;
        if (cp >= 0x1085 && cp <= 0x1086) return GC::Extend;
        if (cp >= 0x109D && cp <= 0x109F) return GC::Extend;
        return GC::SpacingMark; // base consonants, independent vowels
    }

    return GC::Other;
}

// ---------- Grapheme cluster segmentation ----------

// Build cluster boundaries from UTF-8 text. Returns cluster start byte offsets.
// cluster_starts[i] = byte offset where cluster i starts. Size = cluster_count+1.
static void segment_graphemes(const std::string& utf8,
                              std::vector<uint32_t>& cluster_starts) {
    cluster_starts.clear();
    if (utf8.empty()) {
        cluster_starts.push_back(0);
        return;
    }

    const char* p = utf8.data();
    const char* end = p + utf8.size();

    cluster_starts.push_back(0); // cluster 0 starts at byte 0
    uint32_t prev_cp = -1;
    GC prev_gc = GC::Other;
    bool prev_was_cr = false;
    uint32_t ri_count = 0; // regional indicator count (mod 2)

    while (p < end) {
        const char* cluster_start = p;
        int32_t cp = decode_utf8(&p, end);
        if (cp < 0) {
            // Invalid byte: treat as its own cluster.
            cluster_starts.push_back(static_cast<uint32_t>(p - utf8.data()));
            prev_cp = -1;
            prev_gc = GC::Other;
            prev_was_cr = false;
            ri_count = 0;
            continue;
        }

        GC gc = categorize(cp);

        bool break_here = true;

        // GB3: CR x LF
        if (prev_was_cr && gc == GC::LF) break_here = false;

        // GB4: Control x any (after CR, before/after LF)
        if (prev_gc == GC::Control || gc == GC::Control) break_here = true;

        // GB5/GB6/GB7/LGB8 etc. handled by Extend rule
        if (gc == GC::Extend || gc == GC::SpacingMark) break_here = false;

        // GB9: ZWJ x GL_AFTER_ZWJ (simplified: ZWJ stays with prev)
        if (prev_gc == GC::ZWJ) break_here = false;

        // GB11: Regional Indicator pairs
        if (gc == GC::RegionalIndicator) {
            ri_count++;
            if (ri_count % 2 == 1) break_here = false; // first RI stays with prev
        } else {
            ri_count = 0;
        }

        if (break_here) {
            cluster_starts.push_back(static_cast<uint32_t>(p - utf8.data()));
        }

        prev_cp = cp;
        prev_gc = gc;
        prev_was_cr = (gc == GC::CR);
    }
}

TextDocument::TextDocument() {
    // Default to A4 dimensions.
    m_paper_width_mm = 210.0f;
    m_paper_height_mm = 297.0f;
}

void TextDocument::set_cursor(uint32_t cluster_index) {
    if (cluster_index > m_clusters.size()) {
        m_cursor = static_cast<uint32_t>(m_clusters.size());
    } else {
        m_cursor = cluster_index;
    }
}

void TextDocument::insert_utf8(const char* utf8, uint32_t byte_length) {
    if (byte_length == 0 || !utf8) return;

    // Find byte offset of cursor.
    uint32_t cursor_byte = cluster_to_byte(m_cursor);

    // Insert into UTF-8 string.
    m_utf8.insert(cursor_byte, utf8, byte_length);

    // Rebuild clusters (simple approach: re-segment whole text).
    rebuild_clusters();

    // Cursor moves to after inserted text.
    // Re-segmentation may change cluster count; find cursor position after insert.
    // Simple: cursor = old cursor + number of new clusters from inserted text.
    // We approximate by re-finding the byte offset.
    uint32_t new_cursor_byte = cursor_byte + byte_length;
    m_cursor = byte_to_cluster(new_cursor_byte);
}

void TextDocument::backspace() {
    if (m_cursor == 0) return;

    // Byte offset of cluster before cursor.
    uint32_t cluster_byte = cluster_to_byte(m_cursor - 1);
    uint32_t next_cluster_byte = (m_cursor < m_clusters.size())
                                     ? cluster_to_byte(m_cursor)
                                     : static_cast<uint32_t>(m_utf8.size());

    uint32_t delete_len = next_cluster_byte - cluster_byte;
    m_utf8.erase(cluster_byte, delete_len);

    rebuild_clusters();

    // Cursor moves back one cluster.
    if (m_cursor > 0) m_cursor--;
}

void TextDocument::delete_forward() {
    if (m_cursor >= m_clusters.size()) return;

    uint32_t cluster_byte = cluster_to_byte(m_cursor);
    uint32_t next_cluster_byte = (m_cursor + 1 < m_clusters.size())
                                     ? cluster_to_byte(m_cursor + 1)
                                     : static_cast<uint32_t>(m_utf8.size());

    uint32_t delete_len = next_cluster_byte - cluster_byte;
    m_utf8.erase(cluster_byte, delete_len);

    rebuild_clusters();
}

void TextDocument::enter() {
    // Enter splits the current logical line at the cursor.
    // The document text gets a newline character at the cursor position.
    uint32_t cursor_byte = cluster_to_byte(m_cursor);
    m_utf8.insert(cursor_byte, "\n", 1);
    rebuild_clusters();
    // Cursor moves past the newline.
    m_cursor = byte_to_cluster(cursor_byte + 1);
}

void TextDocument::move_left() {
    if (m_cursor > 0) m_cursor--;
}

void TextDocument::move_right() {
    if (m_cursor < m_clusters.size()) m_cursor++;
}

void TextDocument::move_to_line_start() {
    // Find the line containing the cursor, move to its first cluster.
    for (const auto& line : m_lines) {
        if (m_cursor >= line.first_cluster &&
            m_cursor < line.first_cluster + line.cluster_count) {
            m_cursor = line.first_cluster;
            return;
        }
    }
    // If not in any line (e.g. after edit before relayout), move to 0.
    m_cursor = 0;
}

void TextDocument::move_to_line_end() {
    for (const auto& line : m_lines) {
        if (m_cursor >= line.first_cluster &&
            m_cursor < line.first_cluster + line.cluster_count) {
            m_cursor = line.first_cluster + line.cluster_count;
            return;
        }
    }
    m_cursor = static_cast<uint32_t>(m_clusters.size());
}

void TextDocument::move_up() {
    // Find current line, move to previous line at same visual X.
    // Simplified: move to start of previous line.
    for (size_t i = 0; i < m_lines.size(); ++i) {
        if (m_cursor >= m_lines[i].first_cluster &&
            m_cursor < m_lines[i].first_cluster + m_lines[i].cluster_count) {
            if (i > 0) {
                m_cursor = m_lines[i - 1].first_cluster;
            }
            return;
        }
    }
}

void TextDocument::move_down() {
    for (size_t i = 0; i < m_lines.size(); ++i) {
        if (m_cursor >= m_lines[i].first_cluster &&
            m_cursor < m_lines[i].first_cluster + m_lines[i].cluster_count) {
            if (i + 1 < m_lines.size()) {
                m_cursor = m_lines[i + 1].first_cluster;
            }
            return;
        }
    }
}

void TextDocument::set_paper_dims(float width_mm, float height_mm) {
    m_paper_width_mm = width_mm;
    m_paper_height_mm = height_mm;
}

void TextDocument::set_screen_dims(float width_px, float height_px) {
    m_screen_width_px = width_px;
    m_screen_height_px = height_px;
}

void TextDocument::set_rule_gap_mm(float gap_mm) { m_rule_gap_mm = gap_mm; }
void TextDocument::set_rule_top_mm(float top_mm) { m_rule_top_mm = top_mm; }
void TextDocument::set_margin_x_mm(float margin_x_mm) { m_margin_x_mm = margin_x_mm; }
void TextDocument::set_font_size_px(float font_size_px) { m_font_size_px = font_size_px; }

void TextDocument::rebuild_clusters() {
    segment_graphemes(m_utf8, m_cluster_starts_);
    m_clusters.clear();
    m_clusters.reserve(m_cluster_starts_.size() - 1);
    for (size_t i = 0; i + 1 < m_cluster_starts_.size(); ++i) {
        GraphemeCluster gc;
        gc.start_byte = m_cluster_starts_[i];
        gc.byte_length =
            m_cluster_starts_[i + 1] - m_cluster_starts_[i];
        gc.cluster_index = static_cast<uint32_t>(i);
        m_clusters.push_back(gc);
    }
}

uint32_t TextDocument::byte_to_cluster(uint32_t byte_offset) const {
    if (byte_offset >= static_cast<uint32_t>(m_utf8.size())) {
        return static_cast<uint32_t>(m_clusters.size());
    }
    // Binary search in cluster_starts_.
    uint32_t lo = 0;
    uint32_t hi = static_cast<uint32_t>(m_cluster_starts_.size()) - 1;
    while (lo < hi) {
        uint32_t mid = (lo + hi + 1) / 2;
        if (m_cluster_starts_[mid] <= byte_offset) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

uint32_t TextDocument::cluster_to_byte(uint32_t cluster_index) const {
    if (cluster_index >= m_cluster_starts_.size()) {
        return static_cast<uint32_t>(m_utf8.size());
    }
    return m_cluster_starts_[cluster_index];
}

void TextDocument::set_composing(const char* utf8, uint32_t byte_length) {
    if (utf8 && byte_length > 0) {
        m_composing.assign(utf8, byte_length);
    } else {
        m_composing.clear();
    }
    m_composing_cursor = 0;
}

void TextDocument::clear_composing() {
    m_composing.clear();
    m_composing_cursor = 0;
}

bool TextDocument::relayout() {
    // Rebuild clusters from current UTF-8.
    rebuild_clusters();

    // Clamp cursor.
    if (m_cursor > m_clusters.size()) {
        m_cursor = static_cast<uint32_t>(m_clusters.size());
    }

    // Compute line breaks based on ruled-line geometry.
    compute_line_breaks();

    // Compute positioned glyphs (requires HarfBuzz shaping).
    compute_positioned_glyphs();

    return true;
}

void TextDocument::compute_line_breaks() {
    m_lines.clear();

    if (m_clusters.empty()) {
        // One empty line.
        TextLine line;
        line.line_index = 0;
        line.first_cluster = 0;
        line.cluster_count = 0;
        line.baseline_y = m_rule_top_mm * (m_screen_height_px / m_paper_height_mm);
        line.width = 0.0f;
        line.wrapped = false;
        m_lines.push_back(line);
        m_cursor_line = 0;
        m_cursor_x = m_margin_x_mm * (m_screen_width_px / m_paper_width_mm);
        m_cursor_y = line.baseline_y;
        return;
    }

    // Screen space conversion.
    float mm_to_px_x = m_screen_width_px / m_paper_width_mm;
    float mm_to_px_y = m_screen_height_px / m_paper_height_mm;

    float left_margin_px = m_margin_x_mm * mm_to_px_x;
    float rule_gap_px = m_rule_gap_mm * mm_to_px_y;
    float rule_top_px = m_rule_top_mm * mm_to_px_y;
    float font_size_px = m_font_size_px;

    // Compute line height (line box from font metrics).
    float line_height_px = font_size_px * 1.2f; // approx ascent + descent + leading

    // Determine number of ruled lines that fit on the page.
    float available_height_mm = m_paper_height_mm - m_rule_top_mm - 20.0f; // bottom margin
    int max_lines = static_cast<int>(available_height_mm / m_rule_gap_mm) + 1;

    // Simple line breaking: each cluster gets its own advance width estimate.
    // We use average advance per cluster for wrapping decisions.
    float avg_advance_px = font_size_px * 0.6f; // rough estimate
    float line_width_px = (m_paper_width_mm - m_margin_x_mm * 2.0f) * mm_to_px_x;

    uint32_t cluster_idx = 0;
    int line_idx = 0;
    float line_baseline_px = rule_top_px;

    while (cluster_idx < m_clusters.size() && line_idx < max_lines) {
        TextLine line;
        line.line_index = line_idx;
        line.first_cluster = cluster_idx;
        line.baseline_y = line_baseline_px;
        line.wrapped = false;

        float line_x = left_margin_px;
        uint32_t clusters_on_line = 0;

        while (cluster_idx < m_clusters.size()) {
            // Estimate cluster advance width.
            float advance = avg_advance_px;
            // Myanmar clusters tend to be wider.
            uint32_t cb = m_clusters[cluster_idx].start_byte;
            if (cb < m_utf8.size()) {
                unsigned char lead = static_cast<unsigned char>(m_utf8[cb]);
                if (lead >= 0xE0) advance = font_size_px * 0.9f; // multi-byte
            }

            if (clusters_on_line > 0 && line_x + advance > line_width_px + left_margin_px) {
                // Word wrap: break here.
                break;
            }

            line_x += advance;
            cluster_idx++;
            clusters_on_line++;
        }

        line.cluster_count = clusters_on_line;
        line.width = line_x - left_margin_px;
        m_lines.push_back(line);

        line_baseline_px += rule_gap_px;
        line_idx++;
    }

    // Set cursor position.
    m_cursor_line = 0;
    m_cursor_x = left_margin_px;
    m_cursor_y = rule_top_px;

    // Find which line the cursor is on.
    for (size_t i = 0; i < m_lines.size(); ++i) {
        if (m_cursor >= m_lines[i].first_cluster &&
            m_cursor < m_lines[i].first_cluster + m_lines[i].cluster_count) {
            m_cursor_line = static_cast<uint32_t>(i);
            // Compute cursor X within line.
            float cx = left_margin_px;
            for (uint32_t c = m_lines[i].first_cluster;
                 c < m_cursor && c < m_lines[i].first_cluster + m_lines[i].cluster_count;
                 ++c) {
                cx += avg_advance_px;
            }
            m_cursor_x = cx;
            m_cursor_y = m_lines[i].baseline_y;
            break;
        }
    }
}

void TextDocument::compute_positioned_glyphs() {
    // Positioned glyphs require HarfBuzz shaping + SDF atlas.
    // This is called by the integration layer after shaping.
    // For now, clear and let the caller populate.
    m_positioned.clear();
}

} // namespace noat
