# NOAT Project State

## Current Branch
`feat/m1-headless-paper` — school paper shader content system with performance optimization

## Milestones
- **M2 (paper_base)**: Complete — baseline 29.74ms @ 720x1280, 54.78ms @ 1080x1920
- **M3 (paper_content)**: Complete — UBO/precision fix verified on Mali-G610 MC6 (commit `354c6bd`)
- **M4 (paper_wear)**: Baseline exists (153.75ms @ 720x1280, 134.22ms @ 1080x1920) — not yet optimized
- **M5 (Item A - bake)**: Complete — verified on device, ~19x speedup
- **M6 (Item E - idle)**: Complete — verified on device, 1 frame per 60s idle
- **M7 (typed text)**: IN PROGRESS — CI green (run 45939179424, all 3 lanes), baked goldens committed, 4x zoom passes, HarfBuzz linked into Android artifact, text rendering verified on device (English + Myanmar)

## Active Work
Phase 7 (typed handwriting on ruled lines): HarfBuzz shaping + SDF atlas + Myanmar correctness + handwriting deformation — all verified on device

## Blocked
- malioc not available — cycle counts unmeasured in all baselines.

## Key Files
- `shaders/paper_content.frag` — content shader (UBO-fixed, verified)
- `shaders/paper_content_bake.frag` / `paper_content_final.frag` — bake/final shaders (handwriting deformation in final)
- `native/paper-headless/shape_run.h/.cpp` — HarfBuzz complex-script shaping (CI-built only)
- `native/paper-headless/sdf_atlas.h/.cpp` — TrueType -> SDF atlas (CI-built only)
- `native/paper-headless/main.cpp` — headless Vulkan host with `--bench`, `--bake`, `--idle-test`, `--text`
- `reports/optimization.md` — optimization ledger with Item A + Item E results
- `reports/myanmar_correctness_matrix.md` — Myanmar shaping verification results
- `DECISIONS.md` — UBO fix record, bake invalidation rules, golden tier split, HarfBuzz build record
- `goldens/mali/` — locked pre-bake goldens (immutable)
- `goldens/mali/baked/` — approved baked goldens (new tier)
- `goldens/phase7/` — new Phase 7 text goldens (to be created)

## Device
Mali-G610 MC6 (Vulkan 1.1.177), Termux native shell, headless Vulkan (no surface/swapchain)

## Verified on Device
- UBO/precision fix (commit 354c6bd) — paper_lit nonzero
- Item A (bake path) — 10-16x speedup, rmse=0.0000 vs baked goldens
- Item E (idle rendering) — 1 frame per 60s idle
- HarfBuzz shaping — Myanmar + English
- SDF atlas bake — RGBA8 SDF texture
- Handwriting deformation — warp field, per-cluster drift/rotation/size/pressure
- Text rendering — English + Myanmar on device
