#!/usr/bin/env bash
# tools/ci_harfbuzz.sh - Build HarfBuzz for Android arm64 using CMake
# Uses CMake + NDK toolchain (same approach as ci_native.sh) which produces
# ARM64 objects correctly.
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

HARFBUZZ_VERSION="8.4.0"
HARFBUZZ_TARBALL="$OUT_DIR/harfbuzz-$HARFBUZZ_VERSION.tar.xz"
HARFBUZZ_DIR="$OUT_DIR/harfbuzz-$HARFBUZZ_VERSION"

if [ ! -d "$HARFBUZZ_DIR" ]; then
  echo "[ci_harfbuzz] Downloading HarfBuzz $HARFBUZZ_VERSION"
  curl -fsSL --retry 3 -o "$HARFBUZZ_TARBALL" \
    "https://github.com/harfbuzz/harfbuzz/releases/download/$HARFBUZZ_VERSION/harfbuzz-$HARFBUZZ_VERSION.tar.xz"

  if ! head -c 2 "$HARFBUZZ_TARBALL" | od -An -tx1 | grep -q 'fd 37'; then
    echo "[ci_harfbuzz] ERROR: downloaded file is not a valid xz archive" >&2
    head -c 200 "$HARFBUZZ_TARBALL" >&2 || true
    rm -f "$HARFBUZZ_TARBALL"
    exit 1
  fi

  tar -xf "$HARFBUZZ_TARBALL" -C "$OUT_DIR"
fi

if [ ! -f "$HARFBUZZ_DIR/CMakeLists.txt" ]; then
  echo "[ci_harfbuzz] ERROR: CMakeLists.txt not found at $HARFBUZZ_DIR/CMakeLists.txt" >&2
  exit 1
fi

HARFBUZZ_DIR="$(cd "$HARFBUZZ_DIR" && pwd)"

# SHA-256 for DECISIONS.md record
TARBALL_SHA256="$(sha256sum "$HARFBUZZ_TARBALL" | awk '{print $1}')"
echo "[ci_harfbuzz] tarball SHA-256: $TARBALL_SHA256"

# ---- Build for Android arm64 ----
echo "[ci_harfbuzz] configuring HarfBuzz for Android arm64"
cmake -S "$HARFBUZZ_DIR" -B "$OUT_DIR/build-android" \
  -DCMAKE_SYSTEM_NAME=Android \
  -DCMAKE_SYSTEM_VERSION=31 \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DHB_BUILD_TESTS=OFF \
  -DHB_BUILD_UTILS=OFF \
  -DHB_HAVE_FREETYPE=OFF \
  -DHB_HAVE_GLIB=OFF \
  -DHB_HAVE_ICU=OFF \
  -DHB_HAVE_CAIRO=OFF \
  -DHB_HAVE_CORETEXT=OFF \
  -DHB_HAVE_DIRECTWRITE=OFF \
  -DHB_HAVE_UNISCRIBE=OFF \
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

# ---- Verify architecture ----
echo "[ci_harfbuzz] verifying architecture:"
file "$OUT_DIR/libharfbuzz-android-arm64.a"

# ---- Build for host (for shaping tool verification) ----
echo "[ci_harfbuzz] building HarfBuzz for host"
cmake -S "$HARFBUZZ_DIR" -B "$OUT_DIR/build-host" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DHB_BUILD_TESTS=OFF \
  -DHB_BUILD_UTILS=OFF \
  -DHB_HAVE_FREETYPE=OFF \
  -DHB_HAVE_GLIB=OFF \
  -DHB_HAVE_ICU=OFF \
  -DHB_HAVE_CAIRO=OFF \
  -DHB_HAVE_CORETEXT=OFF \
  -DHB_HAVE_DIRECTWRITE=OFF \
  -DHB_HAVE_UNISCRIBE=OFF \
  2>&1 | tail -n 20

cmake --build "$OUT_DIR/build-host" --parallel

HB_HOST_LIB=$(find "$OUT_DIR/build-host" -name "libharfbuzz.a" -print -quit 2>/dev/null || true)
if [ -z "$HB_HOST_LIB" ]; then
  echo "[ci_harfbuzz] ERROR: host libharfbuzz.a not found in $OUT_DIR/build-host" >&2
  exit 1
fi

cp "$HB_HOST_LIB" "$OUT_DIR/libharfbuzz-host.a"
echo "[ci_harfbuzz] wrote $OUT_DIR/libharfbuzz-host.a"

echo "[ci_harfbuzz] done"
