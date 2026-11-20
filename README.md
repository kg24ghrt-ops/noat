# NOAT

An Android application built with Kotlin, XML layouts, and native C++17 modules.
Includes a photorealistic paper-rendering engine (Vulkan) integrated via JNI and
rendered through a `SurfaceView`.

## Project toolchain

- Android Gradle Plugin: 9.3.1
- Gradle: 9.7.0
- Kotlin: 2.4.21
- JDK: 17
- Compile SDK: 36
- Target SDK: 36
- Minimum SDK: 31
- NDK: 30.0.16248370
- CMake: 3.31.6
- Native C++ standard: C++17
- Graphics API: Vulkan 1.1 (via Android NDK)

## Build policy

Android compilation and tests run through GitHub Actions.

No local Android SDK, NDK, CMake installation, Android Studio, or local Gradle compilation is required.

## CI artifacts

The Android CI workflow runs unit tests and produces debug and release APKs.

To retrieve APKs:

1. Open the repository's Actions tab.
2. Select the Android CI workflow run.
3. Download the `noat-android-apks` artifact.

The release APK is unsigned until signing repository secrets are configured.

## Native libraries

- `native-lib` — existing JNI stub (`stringFromJNI`), unchanged.
- `paper-engine` — photorealistic paper rendering engine (C++17, Vulkan 1.1).

## Paper rendering engine (Vulkan)

The paper engine is a GPU-accelerated renderer for physically convincing paper
surfaces, optimized for ARM Mali GPUs using Vulkan's explicit render pass and
subpass architecture.

### Architecture

- **Vulkan 1.1** with `VK_KHR_android_surface` for Android window integration
- **Two-subpass render pass**:
  - Subpass 0 (Geometry): Renders paper surface to color + normal + depth attachments
  - Subpass 1 (Lighting): Reads G-buffer attachments as input attachments (kept in tile memory on Mali GPUs), applies lighting, outputs to swapchain
- **Subpass dependencies** with `VK_DEPENDENCY_BY_REGION_BIT` for fine-grained tile-local synchronization
- **Explicit load/store ops**: `LOAD_OP_CLEAR`/`STORE_OP_DONT_CARE` for transient G-buffer attachments to minimize external memory bandwidth

### Mali-specific optimizations (Vulkan)

- Subpass input attachments keep intermediate data in on-chip tile memory (no external memory round-trip)
- Transient attachments (`VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`) with lazily allocated memory where possible
- `STORE_OP_DONT_CARE` for G-buffer attachments that don't need to persist
- No hardware tessellation (bandwidth-expensive on tile-based GPUs per Arm guidance)

### Rendering features

- Paper surface rendering with procedural fiber normals and surface grain
- Paper-specific dielectric shading (diffuse + low specular + ambient + edge darkening)
- Multiple paper material presets (notebook, premium, printer, recycled,
  watercolor, cotton, handmade, rough, parchment)
- Quality levels: Low, Balanced, High, Ultra

## Application details

- Application ID: `com.example.noat`
- Namespace: `com.example.noat`
- Native libraries: `native-lib`, `paper-engine`
- C++ standard: C++17
- Surface: `SurfaceView` + Vulkan (`com.example.noat.PaperRenderer` via JNI)

## Git workflow

- Work on feature branches.
- Never force-push.
- Never commit secrets, signing keys, tokens, or credentials.
- Inspect diffs and run `git diff --check` before committing.
- Use pull requests targeting `main`.
- Do not claim a build succeeded until GitHub Actions confirms it.
