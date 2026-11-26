#!/usr/bin/env bash
# tools/ci_harfbuzz.sh - Build HarfBuzz for Android arm64 (native) and host
# HarfBuzz is used for complex-script text shaping (Myanmar, English, etc.)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

OUT_DIR="ci-out/harfbuzz"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

# Locate Android NDK
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${ANDROID_NDK:-}}}"
if [ -z "$NDK" ]; then
  NDK="${ANDROID_NDK_ROOT:-}"
fi
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
  tar -xf "$HARFBUZZ_TARBALL" -C "$OUT_DIR"
fi

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
BUILD_DIR="$OUT_DIR/build-android"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

CROSS_FILE="$OUT_DIR/android-arm64-cross.txt"
cat > "$CROSS_FILE" << CROSS_EOF
[binaries]
c = '${TOOLCHAIN_PREFIX}-clang'
cpp = '${TOOLCHAIN_PREFIX}-clang++'
ar = '${LLVM_AR}'
strip = '${LLVM_STRIP}'
pkgconfig = 'false'

[properties]
sys_root = '${NDK}/platforms/android-${ANDROID_API_LEVEL}/arch-arm64'

[host_machine]
system = 'android'
cpu = 'aarch64'
cpu_family = 'aarch64'
endian = 'little'
CROSS_EOF

cd "$BUILD_DIR"
meson setup "$HARFBUZZ_DIR" \
  --cross-file="$CROSS_FILE" \
  --buildtype=release \
  -Ddefault_library=static \
  -Dwith_icu=false \
  -Dwith_freetype=false \
  -Dwith_glib=false \
  -Dwith_coretext=false \
  -Dwith_directwrite=false \
  -Dwith_fontconfig=false \
  -Dwith_builtin_freetype=false \
  -Dtests=false \
  -Dutils=false

meson compile -C "$BUILD_DIR" -j"$(nproc)"

cp "$BUILD_DIR/src/libharfbuzz.a" "$OUT_DIR/libharfbuzz-android-arm64.a"
echo "[ci_harfbuzz] wrote $OUT_DIR/libharfbuzz-android-arm64.a"

# ---- Build for host (for shaping tool) ----
HOST_BUILD_DIR="$OUT_DIR/build-host"
rm -rf "$HOST_BUILD_DIR"
mkdir -p "$HOST_BUILD_DIR"

cd "$HOST_BUILD_DIR"
meson setup "$HARFBUZZ_DIR" \
  --buildtype=release \
  -Ddefault_library=static \
  -Dwith_icu=false \
  -Dwith_freetype=false \
  -Dwith_glib=false \
  -Dwith_coretext=false \
  -Dwith_directwrite=false \
  -Dwith_fontconfig=false \
  -Dwith_builtin_freetype=false \
  -Dtests=false \
  -Dutils=false

meson compile -C "$HOST_BUILD_DIR" -j"$(nproc)"

cp "$HOST_BUILD_DIR/src/libharfbuzz.a" "$OUT_DIR/libharfbuzz-host.a"
echo "[ci_harfbuzz] wrote $OUT_DIR/libharfbuzz-host.a"
