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
