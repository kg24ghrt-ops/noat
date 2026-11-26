#!/usr/bin/env bash
# tools/ci_native.sh - native CI lane: Android arm64 headless + host lavapipe smoke test
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

OUT_DIR="ci-out/native"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

# Locate Android NDK.
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${ANDROID_NDK:-}}}"
if [ -z "$NDK" ]; then
  # android-actions/setup-android exposes ANDROID_NDK_ROOT / ANDROID_NDK_HOME
  NDK="${ANDROID_NDK_ROOT:-}"
fi
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
  echo "[ci_native] ERROR: ANDROID_NDK_ROOT not set" >&2
  exit 1
fi
echo "[ci_native] NDK: $NDK"

# ---- Build HarfBuzz for Android arm64 + host (best-effort) ----
HARFBUZZ_OK=0
echo "[ci_native] building HarfBuzz"
if bash "$REPO_ROOT/tools/ci_harfbuzz.sh" 2>&1; then
  HARFBUZZ_OK=1
  echo "[ci_native] HarfBuzz build succeeded"
else
  echo "[ci_native] WARNING: HarfBuzz build failed, continuing without it"
fi

# Fetch pinned font assets in CI only (used for shaping + SDF atlas goldens).
bash "$REPO_ROOT/tools/ci_fonts.sh"

HARFBUZZ_DIR="$REPO_ROOT/ci-out/harfbuzz"
HARFBUZZ_ANDROID_LIB="$HARFBUZZ_DIR/libharfbuzz-android-arm64.a"
HARFBUZZ_HOST_LIB="$HARFBUZZ_DIR/libharfbuzz-host.a"

HARFBUZZ_INCLUDE=""
if [ "$HARFBUZZ_OK" = "1" ]; then
  # Find HarfBuzz source include directory (tarball may nest differently).
  HARFBUZZ_INCLUDE=$(find "$HARFBUZZ_DIR" -maxdepth 4 -name "hb-blob.h" -print -quit 2>/dev/null | xargs -I{} dirname {} 2>/dev/null || true)
  if [ -z "$HARFBUZZ_INCLUDE" ]; then
    echo "[ci_native] WARNING: cannot find HarfBuzz source headers, building without HarfBuzz"
    HARFBUZZ_OK=0
  else
    echo "[ci_native] HarfBuzz include: $HARFBUZZ_INCLUDE"
  fi
fi

if [ ! -f "$HARFBUZZ_ANDROID_LIB" ]; then
  HARFBUZZ_OK=0
  echo "[ci_native] WARNING: Android HarfBuzz library not found, building without it"
fi

# ---- Android arm64 build ----
ANDROID_DIR="$OUT_DIR/android"
rm -rf "$ANDROID_DIR"
mkdir -p "$ANDROID_DIR"

echo "[ci_native] configuring Android arm64 build"
CMAKE_ARGS=(
  -S native/paper-headless -B "$ANDROID_DIR/build"
  -DCMAKE_SYSTEM_NAME=Android
  -DCMAKE_SYSTEM_VERSION=31
  -DANDROID_ABI=arm64-v8a
  -DANDROID_PLATFORM=android-31
  -DANDROID_NDK="$NDK"
  -DANDROID_STL=c++_static
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_CXX_FLAGS_RELEASE="-O2"
  -DCMAKE_EXE_LINKER_FLAGS_RELEASE="-s"
  -DNOAT_VULKAN_SDK=""
)
if [ "$HARFBUZZ_OK" = "1" ]; then
  CMAKE_ARGS+=(
    -DHARFBUZZ_INCLUDE_DIR="$HARFBUZZ_INCLUDE"
    -DHARFBUZZ_LIBRARY="$HARFBUZZ_ANDROID_LIB"
  )
fi
cmake "${CMAKE_ARGS[@]}" 2>&1 | tail -n 20

echo "[ci_native] building Android headless"
cmake --build "$ANDROID_DIR/build" --config Release -j"$(nproc)"

BIN="$ANDROID_DIR/build/paper-headless"
if [ ! -f "$BIN" ]; then
  echo "[ci_native] ERROR: binary not found at $BIN" >&2
  exit 1
fi
file "$BIN"
strip --strip-all "$BIN" 2>/dev/null || true
cp "$BIN" "$OUT_DIR/headless-arm64"
echo "[ci_native] wrote $OUT_DIR/headless-arm64"

# ---- Host Linux build + lavapipe smoke test (best-effort) ----
HOST_DIR="$OUT_DIR/host"
rm -rf "$HOST_DIR"
mkdir -p "$HOST_DIR"

host_ok=0
if cmake -S native/paper-headless -B "$HOST_DIR/build" -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 && \
   cmake --build "$HOST_DIR/build" --config Release -j"$(nproc)" >/dev/null 2>&1; then
  host_ok=1
  HOST_BIN="$HOST_DIR/build/paper-headless"
  # Produce SPIR-V for host test (need glslang)
  if command -v glslangValidator >/dev/null 2>&1; then
    glslangValidator -V shaders/gradient.frag -o "$HOST_DIR/gradient.frag.spv" 2>/dev/null || true
  fi
  if [ -f "$HOST_BIN" ] && [ -f "$HOST_DIR/gradient.frag.spv" ]; then
    echo "[ci_native] running host smoke test (lavapipe, best-effort)"
    VK_ICD_FILENAMES="" "$HOST_BIN" --shader "$HOST_DIR/gradient.frag.spv" \
      --time 0 --size 720x1280 --out "$HOST_DIR/frame.png" \
      --json "$HOST_DIR/metrics.json" --ascii >"$HOST_DIR/smoke.log" 2>&1 || {
      echo "[ci_native] host smoke test failed (non-fatal):"
      cat "$HOST_DIR/smoke.log" 2>/dev/null || true
    }
  fi
else
  echo "[ci_native] host build skipped (no Vulkan SDK on runner)"
fi

echo "[ci_native] done (host_ok=$host_ok)"
