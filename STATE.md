# NOAT Project State

## Current Branch
`feat/phase7-2-android-text-integration` — school paper shader content system with Android app shell integration (Phase 7.2)

## Milestones
- **M2 (paper_base)**: Complete — baseline 29.74ms @ 720x1280, 54.78ms @ 1080x1920
- **M3 (paper_content)**: Complete — UBO/precision fix verified on Mali-G610 MC6 (commit `354c6bd`)
- **M4 (paper_wear)**: Baseline exists (153.75ms @ 720x1280, 134.22ms @ 1080x1920) — not yet optimized
- **M5 (Item A - bake)**: Complete — verified on device, ~19x speedup
- **M6 (Item E - idle)**: Complete — verified on device, 1 frame per 60s idle
- **M7 (typed text)**: IN PROGRESS — CI green (run 45939179424, all 3 lanes), baked goldens committed, 4x zoom passes, HarfBuzz linked into Android artifact, text rendering verified on device (English + Myanmar)
- **M7.2 (Android app shell)**: IN PROGRESS — text document model, IME integration, caret overlay, ruled-line layout, paper-size calibration, keyboard insets

## Active Work
Phase 7.2 (Android app shell integration): Text document model connected to Android renderer, IME input via hidden EditText, caret overlay with blinking, ruled-line baseline snapping with word wrap, B5 paper size added, CI vulkan header fix.

## Blocked
- malioc not available — cycle counts unmeasured in all baselines.
- Device testing of Android app shell pending CI APK build + physical device.

## Key Files
- `shaders/paper_content.frag` — content shader (UBO-fixed, verified)
- `shaders/paper_content_bake.frag` / `paper_content_final.frag` — bake/final shaders (handwriting deformation in final)
- `native/paper-headless/shape_run.h/.cpp` — HarfBuzz complex-script shaping (CI-built only)
- `native/paper-headless/sdf_atlas.h/.cpp` — TrueType -> SDF atlas (CI-built only)
- `native/paper-headless/main.cpp` — headless Vulkan host with `--bench`, `--bake`, `--idle-test`, `--text`
- `app/src/main/cpp/renderer.h/.cpp` — Android Vulkan renderer with text pipeline + caret overlay + TextDocument
- `app/src/main/cpp/text_document.h/.cpp` — Grapheme-cluster-aware text document model (NEW)
- `app/src/main/cpp/noat_jni.cpp` — JNI bridge: text editing, cursor, composing text, B5 paper size
- `app/src/main/cpp/text_shaper.h/.cpp` — HarfBuzz shaping from app assets
- `app/src/main/cpp/sdf_atlas.h/.cpp` — SDF glyph atlas baking
- `app/src/main/cpp/noat_vulkan.h` — C type declarations for dlsym-based C unit tests (NEW)
- `app/src/main/kotlin/com/example/noat/MainActivity.kt` — Android activity with IME integration, ruled-line layout
- `app/src/main/kotlin/com/example/noat/PaperLib.kt` — Kotlin JNI wrappers (text editing wrappers added)
- `app/src/main/res/layout/activity_main.xml` — SurfaceView + hidden EditText for IME input
- `reports/optimization.md` — optimization ledger with Item A + Item E results
- `reports/myanmar_correctness_matrix.md` — Myanmar shaping verification results
- `DECISIONS.md` — UBO fix record, bake invalidation rules, golden tier split, HarfBuzz build record
- `goldens/mali/` — locked pre-bake goldens (immutable)
- `goldens/mali/baked/` — approved baked goldens (new tier)
- `goldens/phase7/` — new Phase 7 text goldens (to be created)

## Device
Mali-G610 MC6 (Vulkan 1.1.177), Termux native shell, headless Vulkan (no surface/swapchain)

## Verified on Device (headless)
- UBO/precision fix (commit 354c6bd) — paper_lit nonzero
- Item A (bake path) — 10-16x speedup, rmse=0.0000 vs baked goldens
- Item E (idle rendering) — 1 frame per 60s idle
- HarfBuzz shaping — Myanmar + English
- SDF atlas bake — RGBA8 SDF texture
- Handwriting deformation — warp field, per-cluster drift/rotation/size/pressure
- Text rendering — English + Myanmar on device

## Verified in CI
- Shader lane: GLSL -> SPIR-V compilation + validation
- Native lane: Android arm64 headless build with HarfBuzz linked
- APK lane: Gradle assembleDebug
- Android CI: build + C unit tests (vulkan header path fixed)
- C unit tests: noat_vulkan_test.c and noat_engine_test.c compile with NDK Vulkan headers

## Not Yet Device-Verified (Phase 7.2)
- Android app shell GPU text rendering (awaiting CI APK build + physical device)
- IME input with Myanmar Unicode
- Caret blinking overlay
- Keyboard insets + auto-scroll
