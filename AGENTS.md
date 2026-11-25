# NOAT Project Instructions

## Mission
Render hyper-realistic school paper using raw GPU shaders on Android. Native code is a thin harness; Vulkan GLSL shaders are the product. Not a wallpaper, not a 3D engine, not a web frontend.

Default stack: Kotlin/Java thin shell + C++20 headless Vulkan harness + Vulkan GLSL.

## Build environment (hard constraints)
- GitHub Actions is the official build environment. Do NOT compile locally.
- Do not install Android SDK, NDK, CMake, Android Studio, or Vulkan SDK locally.
- Do not run Gradle or native compilation locally. Do not use ADB.
- CI compiles; the phone runs the CI-built Android artifact.
- Keep temp artifacts in `$PREFIX/tmp` or `$HOME/.cache`. Never put executables in `/sdcard` (usually noexec).
- Clean temporary artifacts after tests.

## Approved native libraries only
volk, vk-bootstrap, VMA, stb_image_write, stb_image. No new dependency without asking.
Use VMA for memory (never hand-roll). Do not use Filament/bgfx/The Forge/Unity/Unreal/Godot or any custom Vulkan engine abstraction.

## Headless executable: paper-headless
- Initialize Vulkan, find Mali-G610, no surface, no swapchain, no ANativeWindow.
- One render pass, one subpass, color attachment only, no depth, no MSAA.
- Fullscreen triangle, no vertex/index buffer, loadOp DONT_CARE, storeOp STORE.
- Render into offscreen VkImage, copy to buffer, write PNG, write metrics.json.
- Optional golden diff and ASCII preview.
- CLI: `paper-headless --info` (JSON: device, vulkan_api, headless:true) and render command with `--shader --params --time --size --out --json --golden --ascii`.
- Deterministic: fixed time, fixed seed, fixed resolution, no wall-clock in golden tests.

## CI lanes
- `shader`: GLSL -> SPIR-V (glslangValidator), spirv-val, malioc (best-effort), upload SPIR-V + report.json.
- `native`: build Android arm64 headless executable (static libc++, stripped) + host Linux lavapipe smoke test, upload headless-arm64.
- `apk`: Gradle assembleDebug only.
- Wrapper: `tools/ci <lane>`, `tools/run`.

## Mali-G610 perf rules
mixed precision: highp for positional/noise/hash, mediump for lighting/color where safe. LOD-weighted noise octaves. Avoid discard/dynamic loops/dependent texture reads unless justified. Push constants for hot data, UBO for larger blocks. Fail CI on SPIR-V errors, Vulkan validation errors, golden diff above tolerance, unapproved malioc regression >10%, missing PNG/metrics.

## Git workflow
Work on feature branches. Never force-push. Never commit secrets. Inspect diffs with `git diff --check`. PRs target main. Do not claim build success until GitHub Actions confirms.
