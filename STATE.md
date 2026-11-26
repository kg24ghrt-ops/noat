# NOAT Project State

## Current Branch
`feat/m1-headless-paper` — school paper shader content system with performance optimization

## Milestones
- **M2 (paper_base)**: Complete — baseline 29.74ms @ 720x1280, 54.78ms @ 1080x1920
- **M3 (paper_content)**: Complete — UBO/precision fix verified on Mali-G610 MC6 (commit `354c6bd`)
- **M4 (paper_wear)**: Baseline exists (153.75ms @ 720x1280, 134.22ms @ 1080x1920) — not yet optimized
- **M5 (Item A - bake)**: Complete — verified on device, ~19x speedup
- **M6 (Item E - idle)**: Complete — verified on device, 1 frame per 60s idle
- **M7 (typed text)**: UNBLOCKED — baked goldens committed, 4x zoom check passes. Pending CI green before starting.

## Active Work
Ready to start Phase 7 (typed handwriting) once CI confirms baked goldens + perf gates green.

## Blocked
- malioc not available — cycle counts unmeasured in all baselines.
- Phase 7 cannot start until CI confirms baked goldens pass + perf gates + idle gate all green.

## Key Files
- `shaders/paper_content.frag` — content shader (UBO-fixed, verified)
- `shaders/paper_content_bake.frag` / `paper_content_final.frag` — bake/final shaders
- `native/paper-headless/main.cpp` — headless Vulkan host with `--bench`, `--bake`, `--idle-test`
- `reports/optimization.md` — optimization ledger with Item A + Item E results
- `DECISIONS.md` — UBO fix record, bake invalidation rules, golden tier split
- `goldens/mali/` — locked pre-bake goldens (immutable)
- `goldens/mali/baked/` — approved baked goldens (new tier)

## Device
Mali-G610 MC6 (Vulkan 1.1.177), Termux native shell, headless Vulkan (no surface/swapchain)
