#!/usr/bin/env bash
# tools/ci_harfbuzz.sh - Build HarfBuzz for Android arm64 (native) and host
# HarfBuzz is used for complex-script text shaping (Myanmar, English, etc.)
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

  # Verify the download is a valid tar.xz (not an HTML error page)
  if ! head -c 2 "$HARFBUZZ_TARBALL" | od -An -tx1 | grep -q 'fd 37'; then
    echo "[ci_harfbuzz] ERROR: downloaded file is not a valid xz archive" >&2
    head -c 200 "$HARFBUZZ_TARBALL" >&2 || true
    rm -f "$HARFBUZZ_TARBALL"
    exit 1
  fi

  tar -xf "$HARFBUZZ_TARBALL" -C "$OUT_DIR"
fi

# Verify meson.build exists in the expected source directory.
# GitHub release tarballs may nest differently; search if needed.
if [ ! -f "$HARFBUZZ_DIR/meson.build" ]; then
  FOUND_DIR=$(find "$OUT_DIR" -maxdepth 4 -name meson.build -print -quit 2>/dev/null | xargs -I{} dirname {} 2>/dev/null || true)
  if [ -z "$FOUND_DIR" ]; then
    echo "[ci_harfbuzz] ERROR: cannot find meson.build under $OUT_DIR" >&2
    exit 1
  fi
  echo "[ci_harfbuzz] found meson source at $FOUND_DIR (expected $HARFBUZZ_DIR)"
  HARFBUZZ_DIR="$FOUND_DIR"
fi

# Make HARFBUZZ_DIR absolute before cd into BUILD_DIR (relative paths break).
HARFBUZZ_DIR="$(cd "$HARFBUZZ_DIR" && pwd)"

# Install meson/ninja if needed
if ! command -v meson >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1; then
  echo "[ci_harfbuzz] Installing meson/ninja"
  pip3 install --quiet meson ninja
fi

ANDROID_API_LEVEL=31
TOOLCHAIN_PREFIX="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$ANDROID_API_LEVEL"
LLVM_AR="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar"
LLVM_STRIP="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"

# ---- Build for Android arm64 ----
# Use environment variables for cross-compiler. Let clang's --target flag
# handle sysroot automatically (NDK r27 uses sysroot/ not platforms/).
export CC="$TOOLCHAIN_PREFIX-clang"
export CXX="$TOOLCHAIN_PREFIX-clang++"
export AR="$LLVM_AR"
export STRIP="$LLVM_STRIP"
export PKG_CONFIG_LIBDIR=""
export PKG_CONFIG_PATH=""
export CFLAGS="--target=aarch64-linux-android${ANDROID_API_LEVEL}"
export CXXFLAGS="--target=aarch64-linux-android${ANDROID_API_LEVEL}"
export LDFLAGS="--target=aarch64-linux-android${ANDROID_API_LEVEL}"

BUILD_DIR="$OUT_DIR/build-android"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"
meson setup "$HARFBUZZ_DIR" \
  --buildtype=release \
  -Ddefault_library=static \
  -Dicu=disabled \
  -Dtests=disabled

meson compile -C "$BUILD_DIR" harfbuzz -j"$(nproc)"

cp "$BUILD_DIR/src/libharfbuzz.a" "$OUT_DIR/libharfbuzz-android-arm64.a"
echo "[ci_harfbuzz] wrote $OUT_DIR/libharfbuzz-android-arm64.a"

# ---- Build for host (for shaping tool) ----
unset CC CXX AR STRIP PKG_CONFIG_LIBDIR PKG_CONFIG_PATH CFLAGS CXXFLAGS LDFLAGS

HOST_BUILD_DIR="$OUT_DIR/build-host"
rm -rf "$HOST_BUILD_DIR"
mkdir -p "$HOST_BUILD_DIR"

cd "$HOST_BUILD_DIR"
meson setup "$HARFBUZZ_DIR" \
  --buildtype=release \
  -Ddefault_library=static \
  -Dicu=disabled \
  -Dtests=disabled

meson compile -C "$HOST_BUILD_DIR" harfbuzz -j"$(nproc)"

cp "$HOST_BUILD_DIR/src/libharfbuzz.a" "$OUT_DIR/libharfbuzz-host.a"
echo "[ci_harfbuzz] wrote $OUT_DIR/libharfbuzz-host.a"
