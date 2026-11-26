# NOAT Project Decisions

## Fix: paper_content.frag all-black paper_lit on Mali-G610

**Commit**: `354c6bd` — `fix(m3): fix UBO std140 array stride (arrays->individual floats) and tooth_w smoothstep order`

**Root cause**: Mali-G610 mediump denormal flush / fp16 underflow in the lighting path zeroed `paper_lit`. The GLSL `Params` block used `rule_ink[3]`, `margin_ink[3]`, `pen_color[3]`, `teacher_color[3]` arrays and `int rule_style`/`int margin_enabled`, which under std140 are padded to vec4 stride (16 bytes each). The C++ `Params` struct packed them tightly (4 bytes each). This caused all content fields after offset 12 to be read from wrong UBO locations, producing zero/garbage values in the lighting computation on Mali (lavapipe on host ignores mediump and didn't catch it).

**Changes**:
- `shaders/paper_content.frag`: replaced `rule_ink[3]` → `rule_ink_r/g/b`, `margin_ink[3]` → `margin_ink_r/g/b`, `pen_color[3]` → `pen_color_r/g/b`, `teacher_color[3]` → `teacher_color_r/g/b`; changed `int rule_style`/`int margin_enabled` → `float`; fixed `tooth_w = smoothstep(90.0, 12.0, x)` → `smoothstep(12.0, 90.0, x)` (edge0 > edge1 is undefined on Mali).
- `native/paper-headless/main.cpp`: `Params` struct uses individual float fields matching GLSL std140 exactly; `static_assert` offsets verify layout.

**Device verification** (Mali-G610 MC6, Vulkan 1.1.177):
- Single render: `result: pass`, `paper_lit` nonzero, ASCII shows paper sheet
- Golden diff: rmse=0.9088 (tol=8.0) → pass
- Bench p50=44.71ms / p95=96.55ms at 720x1280

---

## Regression gates

### Host layout test (compile-time)
`native/paper-headless/main.cpp` `Params` struct has `static_assert`s for:
- `sizeof(Params) == 272`
- `offsetof(Params, rule_ink_r) == 48`
- `offsetof(Params, margin_enabled) == 60`
- `offsetof(Params, margin_ink_r) == 68`
- `offsetof(Params, print_fade) == 80`
- `offsetof(Params, light_intensity) == 92`
- `offsetof(Params, pen_color_r) == 144`
- `offsetof(Params, pen_feather) == 156`
- `offsetof(Params, teacher_color_r) == 164`
- `offsetof(Params, teacher_marks) == 176`

These must match the GLSL `Params` std140 block field order exactly. Any change to the GLSL block must be mirrored in the C++ struct and vice versa.

### Device gate
For `paper_content.frag` at 720x1280 with `params/paper_content.json`:
- `paper_lit` must be nonzero (ASCII preview shows paper sheet, not all black)
- Golden diff rmse ≤ 8.0 vs `goldens/mali/paper_content_720x1280.png`
- `result: pass` in metrics.json

---

## Precision guardrail

- Lighting accumulations (`diffuse`, `key`, `fill`, `paper_lit`, `sheen_col`) stay `highp`.
- `mediump` only for colors where validated (base albedo, desk_color, ruling_col).
- `hash2`/`noise2`/`fbm_lod` use `highp` for coordinates/noise/hash.
- `tooth_w` uses `highp` smoothstep with correct edge0 < edge1 ordering.
- Device is Vulkan 1.1.177 — stay core 1.1. Do not require 1.2/1.3-only features.

### fp16 stress test case
Add a render case at 1080x1920 with extreme lighting (light_intensity=2.0, sheen=1.0, exposure=1.5) to confirm no mediump underflow produces black regions.

---

## Item A — Static/dynamic split (bake path)

**Bake invalidation rules:**
- **Light/view changes** → do NOT re-bake; only the final pass runs (~1.4-6.4ms)
- **Seed/param/content changes** → DO re-bake (~138ms one-time at 720x1280, ~340ms at 1080x1920)

**Bake resource cost:**
| Resolution | Texture dims | Memory bytes | Memory (approx) |
|-----------|-------------|-------------|-----------------|
| 720x1280 | 720×1280 RGBA8 | 3,686,400 | ~3.52 MB |
| 1080x1920 | 1080×1920 RGBA8 | 8,294,400 | ~7.91 MB |

**Baked per-frame GPU time (`--bench --bake`, final pass only):**
| Resolution | gpu_ms_p50 | gpu_ms_p95 | Target | Status |
|-----------|-----------|-----------|--------|--------|
| 720x1280 | 1.42ms | 2.68ms | ≤6ms | PASS |
| 1080x1920 | 4.73ms | 6.37ms | ≤12ms | PASS |

**Visual equivalence vs locked pre-bake goldens:**
- 720x1280: rmse=9.6066 (tol=8.0) → FAIL vs locked golden
- 1080x1920: rmse=9.5798 (tol=8.0) → FAIL vs locked golden
- **Bake path is a separate tier** with its own goldens:
  - `goldens/mali/baked/paper_content_720x1280.png` (rmse=0.0000 vs baked output) ✓
  - `goldens/mali/baked/paper_content_1080x1920.png` (rmse=0.0000 vs baked output) ✓
- Locked pre-bake goldens `goldens/mali/paper_content_*.png` are immutable — NOT overwritten.
- Diff source: bilinear texture filtering in contact shadow/desk edges only (sub-perceptual).
- Per-region RMSE: paper/fiber/tooth ~0.0044, ruling ~0.0157, desk/contact shadow ~0.0665.
- Zoom consistency (0.5x→4x): RMSE stable at 0.0323–0.0329. No bake-resolution reveal or LOD popping.

**Region-specific tolerances (baked path):**
- paper / fiber / tooth / ruling / ink: strict (rmse ≤ 0.02 normalized)
- desk / contact shadow: approved filtering tolerance (rmse ≤ 0.10 normalized)

**Constraint:** Full-page re-bake must NOT become the typing path later. Phase 7 text edits will require incremental dirty-rect baking, not full-page bake.

**CI gates (both paths):**
- Procedural path vs locked pre-bake goldens (`goldens/mali/paper_content_*.png`)
- Baked path vs baked goldens (`goldens/mali/baked/paper_content_*.png`)
- Perf gates: 720x1280 p95 ≤6ms, 1080x1920 p95 ≤12ms
- Idle dirty-flag gate: ≤1 rendered frame per 60s idle

---

## Phase 7 — Typed Handwriting (Phase 6 bake accepted, Item E accepted)

### Shaping: HarfBuzz everywhere (Option A)
- HarfBuzz is MIT-licensed; pinned version; built in CI only.
- Linked into both headless executable and Android app native library.
- Android platform shaping is only a test oracle, not production path.
- Source files: `native/paper-headless/shape_run.h/.cpp` (already in CMake build).
- Shape first, deform second. Never jitter per codepoint.

### Fonts (approved, pinned, OFL)
1. **Noto Sans Myanmar** — SIL OFL; Myanmar shaping + rasterization
2. **Patrick Hand** — SIL OFL; Latin handwriting appearance
- Do not use system fonts in production shaping.
- If Patrick Hand lacks required glyphs/metrics, propose one OFL Latin handwriting replacement and ask before switching.

### Mobile calibration defaults
- Write-zoom ruling gap target: ≈44.7 dp (minimum comfortable writing target)
- Approximate writing pan window: ≈51 mm
- Myanmar on narrow 6.35 mm ruling: default to every-other line
- If Myanmar metrics require more vertical space, prefer skipping lines or increasing zoom over shrinking below legibility.

### Zawgyi policy (v1 Unicode-only)
- Detect Zawgyi input; warn the user.
- Do not silently mangle it.
- Do not add a Zawgyi converter without approval.

### Myanmar correctness (non-negotiable)
- Stacked/subjoined consonants, kinzi, medial signs, reordered vowel signs, above/below-base marks, tone marks, asat, Myanmar digits, Myanmar punctuation.
- **Shape first, deform second.**
- Deform at grapheme-cluster level; all glyphs in one cluster share warp/rotation/offset.
- Smooth low-frequency warp field; neighbouring clusters move coherently.
- No Latin cursive slant or connecting strokes on Myanmar.
- Broken stacks, detached marks, or incorrect reordering = fail.

### Text editing rules
- Android IME handles input.
- Caret movement and backspace operate on grapheme clusters, never strand a mark.
- Do not deform uncommitted IME composing text; composing text stable and readable.
- Deform only committed text.

### Line constraint (hard product rule)
- Text only on ruled lines, within margins.
- Left bound = margin line + padding; right bound = sheet edge − padding.
- One logical line per ruled line; word wrap → next ruled line.
- Enter → next ruled line; backspace at line start → previous line's end.
- Caret only on valid baseline slots; page overflow → new page.
- Myanmar line box from real font metrics; stacked consonants/below-base marks never clipped.
- If line box doesn't fit ruling gap: auto-fit scale / auto-select wider ruling / every-other line (narrow 6.35 mm default).

### Realism model (procedural only, no datasets/ML/data/network)
- SDF glyph atlas + positioned glyph instances + shader-side handwriting deformation.
- Bake SDF atlas once per font/size; Myanmar stacks/marks become atlas entries keyed by glyph ID.
- Typed text = positioned glyph instances with cluster IDs.
- Deformation stack: low-frequency warp field, per-cluster baseline drift, small rotation, ±3–5% size variation, advance variation, stroke-width modulation, ink density/pressure variation, pen-lift gaps, entry/exit hooks, ink feathering, graphite tooth adhesion.
- Pencil mode and pen mode.
- Seeded and deterministic: same seed + same text = identical pixels.

### Performance (no regression against locked/baked goldens)
- Text edits use incremental dirty-rect baking; no full-page re-bake per keystroke.
- Partial texture upload on dirty rect; coalesce updates during fast typing.
- Caret blink = cheap overlay; no paper re-render.
- Keystroke → pixel ≤32 ms (aim ≤16 ms).
- Idle GPU work near zero.
- No per-keystroke allocations.


