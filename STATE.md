# NOAT Project State

## Current Branch
`feat/m1-headless-paper` — school paper shader content system with performance optimization

## Milestones
- **M2 (paper_base)**: Complete — baseline 29.74ms @ 720x1280, 54.78ms @ 1080x1920
- **M3 (paper_content)**: Complete — UBO/precision fix verified on Mali-G610 MC6 (commit `354c6bd`)
- **M4 (paper_wear)**: Baseline exists (153.75ms @ 720x1280, 134.22ms @ 1080x1920) — not yet optimized
- **M5 (Item A - bake)**: Complete — verified on device, ~19x speedup
- **M6 (Item E - idle)**: Complete — verified on device, 1 frame per 60s idle
- **M7 (typed text)**: NOT STARTED — pending approval

## Active Work
None — Item A and Item E both verified on Mali-G610 MC6 device.

## Blocked
- Item A golden replacement: baked output differs from locked pre-bake goldens by rmse≈9.6 (tol=8.0). Awaiting approval to replace `goldens/mali/paper_content_720x1280.png` and `paper_content_1080x1920.png` with baked-path equivalents.
- malioc not available — cycle counts unmeasured in all baselines.

## Key Files
- `shaders/paper_content.frag` — content shader (UBO-fixed, verified)
- `shaders/paper_content_bake.frag` / `paper_content_final.frag` — bake/final shaders
- `native/paper-headless/main.cpp` — headless Vulkan host with `--bench`, `--bake`, `--idle-test`
- `reports/optimization.md` — optimization ledger with Item A + Item E results
- `DECISIONS.md` — UBO fix record, bake invalidation rules
- `goldens/mali/` — reference PNGs for golden diff testing

## Device
Mali-G610 MC6 (Vulkan 1.1.177), Termux native shell, headless Vulkan (no surface/swapchain)
