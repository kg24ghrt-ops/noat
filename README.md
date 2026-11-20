# NOAT

An Android application built with Kotlin, XML layouts, and native C++17 modules.
Includes a photorealistic paper-rendering engine (OpenGL ES 3.0–3.2) integrated
via JNI and rendered through a `GLSurfaceView`.

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
- `paper-engine` — photorealistic paper rendering engine (C++17, OpenGL ES 3.0–3.2).

## Paper rendering engine

The paper engine is a GPU-accelerated renderer for physically convincing paper
surfaces, optimized for ARM Mali GPUs (tile-based deferred rendering).

### Architecture

- **Tier A (GLES 3.0 baseline)** — CPU-subdivided paper geometry, normal mapping,
  forward rendering, dielectric paper shading model. Works on all devices.
- **Tier B (GLES 3.1)** — compute shader procedural texture generation
  (fiber/grain textures), enabled when available.
- **Tier C (GLES 3.2)** — hardware tessellation + displacement. Opt-in only;
  disabled by default per Arm Mali guidance (tessellation is bandwidth-expensive
  on tile-based GPUs).

### Mali-specific optimizations

- ASTC texture compression preferred (ETC2 fallback).
- Transient framebuffer attachments invalidated each frame (`glInvalidateFramebuffer`).
- No depth prepass (Arm recommends against on Mali).
- `mediump` precision for colors/lighting, `highp` for positions.
- Render passes organized to minimize external memory bandwidth.

### Rendering features

- Flat, curved, and edge-curled paper geometry with adaptive subdivision.
- Procedural fiber normals and surface grain (procedural noise).
- Paper-specific dielectric shading (diffuse + low specular + ambient + edge darkening).
- Multiple paper material presets (notebook, premium, printer, recycled,
  watercolor, cotton, handmade, rough, parchment).
- OpenGL ES capability detection at runtime (never assumes features exist).

## Application details

- Application ID: `com.example.noat`
- Namespace: `com.example.noat`
- Native libraries: `native-lib`, `paper-engine`
- C++ standard: C++17
- GLSurfaceView: `com.example.noat.PaperRenderer`

## Git workflow

- Work on feature branches.
- Never force-push.
- Never commit secrets, signing keys, tokens, or credentials.
- Inspect diffs and run `git diff --check` before committing.
- Use pull requests targeting `main`.
- Do not claim a build succeeded until GitHub Actions confirms it.
