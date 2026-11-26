# NOAT Milestone 6 — Optimization Report

## Baseline Measurements (Mali-G610, CI-built headless-arm64)

All times measured with `--bench --warmup 10 --frames 60` via `vkQueueSubmit` + `vkWaitForFences` wall-clock timing (vkCmdWriteTimestamp segfaults on Mali-G610; see notes below).

| Scenario | Resolution | p50 ms | p95 ms | FPS (p95) | Pixels |
|----------|-----------|--------|--------|-----------|---------|
| paper_base | 720x1280 | 6.74 | 8.43 | ~118 | 0.92M |
| paper_base | 1080x1920 | 13.98 | 16.07 | ~62 | 2.07M |
| paper_content | 720x1280 | 45.57 | 51.71 | ~19 | 0.92M |
| paper_content | 1080x1920 | 97.76 | 115.95 | ~8.6 | 2.07M |
| paper_wear | 720x1280 | 44.39 | 56.17 | ~17 | 0.92M |
| paper_wear | 1080x1920 | 97.35 | 121.97 | ~8.2 | 2.07M |

## Performance Targets

| Target | Required | Current | Status |
|--------|----------|---------|--------|
| 720x1280 Standard p95 | ≤ 6 ms | 8.43 ms | FAIL (40% over) |
| 1080x1920 Fine p95 | ≤ 12 ms | 16.07 ms | FAIL (34% over) |
| Idle static page | near-zero GPU | continuous render | FAIL |

## Ranked Hotspot List

### 1. Fragment ALU from procedural noise (paper_content, paper_wear) — PRIMARY

The fragment shader evaluates:
- `fbm_lod()` × 3 (formation, fiber-bump, tooth-height): 5 octaves each = 15 `noise2()` calls
- `noise2()` itself: `hash2()` × 4 + `mix()` × 3 + arithmetic ≈ 15 ALU ops per call
- `ridged()` for fiber: `noise2()` + abs + subtract
- `word_cluster()` → `stroke_mark()`: multiple `sin()`, `dot()`, `clamp()`, `smoothstep()` per stroke
- Per-pixel: roughly 50-100 highp transcendental/noise ops

**Estimated cost**: ~35-80ms of the 45-97ms total for content/wear scenarios.

### 2. Fragment ALU from paper_base noise — SECONDARY

- `fbm_lod()` × 2 (formation, tooth): 10 `noise2()` calls
- `ridged()` for fiber
- Per-pixel: ~30-50 ALU ops

**Estimated cost**: ~5-8ms of the 7-16ms total for base scenario.

### 3. Render pass / frame overhead — MINOR

- One render pass, one subpass, color attachment only
- No depth, no MSAA, no extra passes
- Fullscreen triangle (3 vertices)
- Overhead is negligible compared to fragment shader

### 4. Memory bandwidth — MINOR

- Single offscreen color image (R8G8B8A8_UNORM)
- Readback to host buffer (only for non-bench mode)
- No texture fetches in current shader (all procedural)
- Bandwidth is not the bottleneck; ALU is.

## Where the Time Actually Goes

**Fragment ALU dominates.** On Mali-G610 (Valhall), fragment shaders execute per-pixel in tile memory. The paper_content and paper_wear shaders execute ~50-100 noise/hash/transcendental operations per pixel. At 2.07M pixels (1080x1920), that's ~100-200 billion ALU ops per frame. At ~115ms/frame, that's ~1.7 TFLOPS effective, which is plausible for Mali-G610 MC6.

The paper_base shader is cheaper (~30-50 ALU ops/pixel) but still fragment-bound at 7-16ms.

## Expected Optimization Wins (Backlog Order)

### Item A: Static/dynamic split — BIGGEST EXPECTED WIN

Almost everything in the shader is static per frame:
- Paper stock (warmth, formation, fiber, specks)
- Ruling lines (position, waviness, density, fade)
- Margin line
- Content (graphite clusters, pen ink, teacher marks)
- Wear (coffee rings, water stains, foxing, fingerprints, smudge, eraser, edge aging)

Only lighting direction/intensity and tooth_w (footprint-dependent) change per frame.

**Plan**: Bake static layers into textures:
1. Paper albedo texture (stock + formation + fiber + specks + ruling + margin + ink + stains)
2. Micro-height/normal texture
3. Content mask texture (graphite + pen + teacher)

Per-frame shader becomes: texture fetches + lighting math only.

**Expected win**: 5-10x reduction in fragment ALU for content/wear. Could bring 720x1280 content from ~50ms to ~5-10ms.

**Risks**: Bake resolution must survive 4x zoom without visible bake artifacts. Need LOD/mipmaps. Hybrid approach: baked low/mid freq + procedural high-freq tooth gated by zoom.

### Item B: Noise cost — SECONDARY

If procedural noise is retained for some features:
- Replace multi-hash value noise with tiling noise LUT texture (hardware bilinear interpolation)
- Compute each noise field once, reuse across features
- Uniform branch for octave skipping (mmpp is uniform)
- Remove `pow`, `sin`, `exp`, integer modulo from inner loops

**Expected win**: 2-3x ALU reduction if procedural noise retained.

### Item C: Precision — TERTIARY

Confirm lighting/color runs fp16 on Valhall via malioc. Keep coordinates/noise/hash as highp.

**Expected win**: Minor cycle reduction, mainly on Mali where fp16 is 2x throughput.

### Items D-G: Lower priority

- Passes/bandwidth: Already optimal (1 pass, color only)
- App shell dirty-flag: Important for idle power but doesn't affect headless bench
- Shader compilation: spirv-opt -O could help minor
- Quality tiers: Needed for production but not for headless bench

## Items Tried That Didn't Work

None yet — this is the first optimization pass.

## Remaining Known Bottlenecks

1. Fragment ALU from procedural noise (primary)
2. No texture fetches — everything computed per-pixel
3. No idle/dirty-flag rendering in app shell
4. malioc not available locally (ARM binary, no local toolchain) — cycle counts unmeasured

## Next Steps

1. Implement Item A: bake static layers to textures
2. Implement Item B: noise LUT texture if procedural retained
3. Implement Item C: confirm fp16 lighting via malioc
4. Implement Item E: dirty-flag rendering in app shell
5. Re-run benchmarks and compare against baselines
