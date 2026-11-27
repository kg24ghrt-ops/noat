#!/usr/bin/env bash
# tools/ci_harfbuzz.sh - Build HarfBuzz for Android arm64 (native) using CMake
# CMake build avoids Meson's cross-compilation sanity check (which tries to
# execute ARM64 binaries on the x86_64 host).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

OUT_DIR="$REPO_ROOT/ci-out/harfbuzz"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

# Locate Android NDK
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${ANDROID_NDK:-}}}"
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
  echo "[ci_harfbuzz] ERROR: ANDROID_NDK_ROOT not set" >&2
  exit 1
fi
echo "[ci_harfbuzz] NDK: $NDK"

TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake"
if [ ! -f "$TOOLCHAIN_FILE" ]; then
  echo "[ci_harfbuzz] ERROR: NDK toolchain file not found at $TOOLCHAIN_FILE" >&2
  exit 1
fi

HARFBUZZ_VERSION="8.4.0"
HARFBUZZ_TARBALL="$OUT_DIR/harfbuzz-$HARFBUZZ_VERSION.tar.xz"
HARFBUZZ_DIR="$OUT_DIR/harfbuzz-$HARFBUZZ_VERSION"

if [ ! -d "$HARFBUZZ_DIR" ]; then
  echo "[ci_harfbuzz] Downloading HarfBuzz $HARFBUZZ_VERSION"
  curl -fsSL --retry 3 -o "$HARFBUZZ_TARBALL" \
    "https://github.com/harfbuzz/harfbuzz/releases/download/$HARFBUZZ_VERSION/harfbuzz-$HARFBUZZ_VERSION.tar.xz"

  # Verify the download is a valid tar.xz (not an HTML error page)
  if ! head -c 2 "$HARFBUZZ_TARBALL" | od -An -tx1 | grep -q 'fd 37'; then
    echo "[ci_harfbuzz] ERROR: downloaded file is not a valid xz archive" >&2
    head -c 200 "$HARFBUZZ_TARBALL" >&2 || true
    rm -f "$HARFBUZZ_TARBALL"
    exit 1
  fi

  tar -xf "$HARFBUZZ_TARBALL" -C "$OUT_DIR"
fi

# Verify CMakeLists.txt exists
if [ ! -f "$HARFBUZZ_DIR/CMakeLists.txt" ]; then
  FOUND_DIR=$(find "$OUT_DIR" -maxdepth 4 -name CMakeLists.txt -print -quit 2>/dev/null | xargs -I{} dirname {} 2>/dev/null || true)
  if [ -z "$FOUND_DIR" ]; then
    echo "[ci_harfbuzz] ERROR: cannot find CMakeLists.txt under $OUT_DIR" >&2
    exit 1
  fi
  echo "[ci_harfbuzz] found CMake source at $FOUND_DIR (expected $HARFBUZZ_DIR)"
  HARFBUZZ_DIR="$FOUND_DIR"
fi

# Make HARFBUZZ_DIR absolute
HARFBUZZ_DIR="$(cd "$HARFBUZZ_DIR" && pwd)"

# ---- Build for Android arm64 ----
echo "[ci_harfbuzz] configuring HarfBuzz for Android arm64"
cmake -S "$HARFBUZZ_DIR" -B "$OUT_DIR/build-android" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
  -DANDROID_ABI=arm64-v8a \
  -DCMAKE_ANDROID_ARCH_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Release \
  -DHB_BUILD_TESTS=OFF \
  -DHB_BUILD_UTILS=OFF \
  -DHB_HAVE_FREETYPE=OFF \
  -DHB_HAVE_GLIB=OFF \
  -DHB_HAVE_ICU=OFF \
  -DHB_HAVE_CORETEXT=OFF \
  -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
  -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
  2>&1 | tail -n 20

echo "[ci_harfbuzz] building HarfBuzz"
cmake --build "$OUT_DIR/build-android" --parallel

HB_ANDROID_LIB=$(find "$OUT_DIR/build-android" -name "libharfbuzz.a" -print -quit 2>/dev/null || true)
if [ -z "$HB_ANDROID_LIB" ]; then
  echo "[ci_harfbuzz] ERROR: libharfbuzz.a not found in $OUT_DIR/build-android" >&2
  exit 1
fi

cp "$HB_ANDROID_LIB" "$OUT_DIR/libharfbuzz-android-arm64.a"
echo "[ci_harfbuzz] wrote $OUT_DIR/libharfbuzz-android-arm64.a"

# ---- Build for host (for shaping tool verification) ----
echo "[ci_harfbuzz] configuring HarfBuzz for host"
cmake -S "$HARFBUZZ_DIR" -B "$OUT_DIR/build-host" \
  -DCMAKE_BUILD_TYPE=Release \
  -DHB_BUILD_TESTS=OFF \
  -DHB_BUILD_UTILS=OFF \
  -DHB_HAVE_FREETYPE=OFF \
  -DHB_HAVE_GLIB=OFF \
  -DHB_HAVE_ICU=OFF \
  -DHB_HAVE_CORETEXT=OFF \
  2>&1 | tail -n 20

cmake --build "$OUT_DIR/build-host" --parallel

HB_HOST_LIB=$(find "$OUT_DIR/build-host" -name "libharfbuzz.a" -print -quit 2>/dev/null || true)
if [ -z "$HB_HOST_LIB" ]; then
  echo "[ci_harfbuzz] ERROR: host libharfbuzz.a not found in $OUT_DIR/build-host" >&2
  exit 1
fi

cp "$HB_HOST_LIB" "$OUT_DIR/libharfbuzz-host.a"
echo "[ci_harfbuzz] wrote $OUT_DIR/libharfbuzz-host.a"
