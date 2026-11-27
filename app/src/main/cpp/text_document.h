#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace noat {

// A single grapheme cluster: one or more Unicode codepoints that form a
// user-perceived character. Myanmar stacks/medial/vowel signs are kept
// together as one cluster.
struct GraphemeCluster {
    uint32_t start_byte = 0;   // byte offset in document UTF-8
    uint32_t byte_length = 0;  // byte length in UTF-8
    uint32_t cluster_index = 0; // dense index within document
};

// One logical line of text.
struct TextLine {
    uint32_t line_index = 0;
    uint32_t first_cluster = 0;  // cluster index of first grapheme on line
    uint32_t cluster_count = 0;  // number of graphemes on line
    float baseline_y = 0.0f;     // screen Y of ruled baseline
    float width = 0.0f;          // total advance width in screen px
    bool wrapped = false;        // true if this line was created by word wrap
};

// A glyph instance positioned for rendering, derived from HarfBuzz shaping.
struct PositionedGlyph {
    float x = 0.0f;       // screen X (cluster warp already applied)
    float y = 0.0f;       // screen Y (baseline + drift)
    float w = 0.0f;       // glyph width in screen px
    float h = 0.0f;       // glyph height in screen px
    float ax = 0.0f;      // atlas rect X
    float ay = 0.0f;      // atlas rect Y
    float aw = 0.0f;      // atlas rect W
    float ah = 0.0f;      // atlas rect H
    uint32_t glyph_id = 0;
    uint32_t cluster_id = 0;
    float advance_x = 0.0f; // advance to next glyph (screen px)
};

// TextDocument: stable document state independent of rendering.
// UTF-8 stored, grapheme-cluster-aware cursor/backspace, multiple logical lines.
class TextDocument {
public:
    TextDocument();
    ~TextDocument() = default;

    // UTF-8 accessors.
    const std::string& utf8() const { return m_utf8; }
    uint32_t byte_length() const { return static_cast<uint32_t>(m_utf8.size()); }
    uint32_t cluster_count() const { return m_clusters.size(); }

    // Grapheme-cluster-aware cursor: cursor_index is a dense cluster index
    // (0..cluster_count). cursor 0 = before first cluster, cursor N = after last.
    uint32_t cursor() const { return m_cursor; }
    void set_cursor(uint32_t cluster_index);

    // Insert UTF-8 text at cursor (grapheme-aware: cursor stays after inserted text).
    void insert_utf8(const char* utf8, uint32_t byte_length);

    // Backspace: delete the grapheme cluster before the cursor.
    void backspace();

    // Delete forward: delete the grapheme cluster at/after the cursor.
    void delete_forward();

    // Enter: split current logical line at cursor.
    void enter();

    // Move cursor left by one grapheme cluster.
    void move_left();

    // Move cursor right by one grapheme cluster.
    void move_right();

    // Move cursor to start of current line.
    void move_to_line_start();

    // Move cursor to end of current line.
    void move_to_line_end();

    // Move cursor up one line (same visual X if possible).
    void move_up();

    // Move cursor down one line.
    void move_down();

    // Set ruled-line geometry for layout.
    void set_paper_dims(float width_mm, float height_mm);
    void set_screen_dims(float width_px, float height_px);
    void set_rule_gap_mm(float gap_mm);
    void set_rule_top_mm(float top_mm);
    void set_margin_x_mm(float margin_x_mm);
    void set_font_size_px(float font_size_px);

    // Re-layout: reshape text, compute line breaks, positioned glyphs.
    // Call after any document edit. Returns true on success.
    bool relayout();

    // Access positioned glyphs for GPU upload.
    const std::vector<PositionedGlyph>& positioned_glyphs() const { return m_positioned; }
    uint32_t positioned_count() const { return static_cast<uint32_t>(m_positioned.size()); }

    // Access logical lines.
    const std::vector<TextLine>& lines() const { return m_lines; }

    // Cursor line index (for caret placement).
    uint32_t cursor_line() const { return m_cursor_line; }
    // Cursor X within line (for caret placement).
    float cursor_x() const { return m_cursor_x; }
    // Cursor Y (baseline) for caret placement.
    float cursor_y() const { return m_cursor_y; }

    // Committed vs composing text: composing text is NOT deformed.
    void set_composing(const char* utf8, uint32_t byte_length);
    void clear_composing();
    const std::string& composing() const { return m_composing; }
    bool has_composing() const { return !m_composing.empty(); }

    // Deterministic seed for handwriting deformation (same = same pixels).
    void set_seed(uint32_t seed) { m_seed = seed; }
    uint32_t seed() const { return m_seed; }

private:
    std::string m_utf8;              // committed text (UTF-8)
    std::vector<uint32_t> m_cluster_starts_; // cluster start byte offsets
    std::vector<GraphemeCluster> m_clusters; // grapheme cluster boundaries
    uint32_t m_cursor = 0;           // cursor cluster index

    // Composing (IME) text - stored separately, NOT deformed.
    std::string m_composing;
    uint32_t m_composing_cursor = 0;

    // Logical lines (re-computed on relayout).
    std::vector<TextLine> m_lines;
    uint32_t m_cursor_line = 0;
    float m_cursor_x = 0.0f;
    float m_cursor_y = 0.0f;

    // Positioned glyphs for GPU (re-computed on relayout).
    std::vector<PositionedGlyph> m_positioned;

    // Layout parameters.
    float m_paper_width_mm = 210.0f;
    float m_paper_height_mm = 297.0f;
    float m_screen_width_px = 720.0f;
    float m_screen_height_px = 1280.0f;
    float m_rule_gap_mm = 7.1f;
    float m_rule_top_mm = 25.4f;
    float m_margin_x_mm = 31.75f;
    float m_font_size_px = 24.0f;
    uint32_t m_seed = 4321;

    // Helpers.
    void rebuild_clusters();
    uint32_t byte_to_cluster(uint32_t byte_offset) const;
    uint32_t cluster_to_byte(uint32_t cluster_index) const;
    void compute_line_breaks();
    void compute_positioned_glyphs();
};

} // namespace noat
