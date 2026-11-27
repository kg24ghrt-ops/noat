#!/usr/bin/env bash
# tools/ci_harfbuzz.sh - Build HarfBuzz for Android arm64 (native) and host
# Uses Meson with skip_sanity_check to avoid cross-compile host execution.
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

TOOLCHAIN_PREFIX="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31"

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

if [ ! -f "$HARFBUZZ_DIR/meson.build" ]; then
  FOUND_DIR=$(find "$OUT_DIR" -maxdepth 4 -name meson.build -print -quit 2>/dev/null | xargs -I{} dirname {} 2>/dev/null || true)
  if [ -z "$FOUND_DIR" ]; then
    echo "[ci_harfbuzz] ERROR: cannot find meson.build under $OUT_DIR" >&2
    exit 1
  fi
  echo "[ci_harfbuzz] found meson source at $FOUND_DIR (expected $HARFBUZZ_DIR)"
  HARFBUZZ_DIR="$FOUND_DIR"
fi

HARFBUZZ_DIR="$(cd "$HARFBUZZ_DIR" && pwd)"

# Install meson/ninja if needed
if ! command -v meson >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1; then
  echo "[ci_harfbuzz] Installing meson/ninja"
  pip3 install --quiet meson ninja
fi
# ---- Build for Android arm64 ----
echo "[ci_harfbuzz] configuring HarfBuzz for Android arm64 (Meson)"

# Create clang wrapper that forces --target=aarch64-linux-android31.
# Meson cross-file c_args don't reliably reach the compiler invocation,
# so we wrap clang to inject --target on every call.
CLANG_WRAPPER="$OUT_DIR/clang-aarch64.sh"
CLANGPP_WRAPPER="$OUT_DIR/clang++-aarch64.sh"
cat > "$CLANG_WRAPPER" << WRAPPER_EOF
#!/bin/bash
exec '$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang' --target=aarch64-linux-android31 "\$@"
WRAPPER_EOF
cat > "$CLANGPP_WRAPPER" << WRAPPER_EOF
#!/bin/bash
exec '$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++' --target=aarch64-linux-android31 "\$@"
WRAPPER_EOF
chmod +x "$CLANG_WRAPPER" "$CLANGPP_WRAPPER"

ANDROID_CROSS_FILE="$OUT_DIR/android-arm64-cross.txt"
cat > "$ANDROID_CROSS_FILE" << CROSS_EOF
[binaries]
c = '$CLANG_WRAPPER'
cpp = '$CLANGPP_WRAPPER'
ar = '$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar'
strip = '$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip'
pkgconfig = 'false'

[properties]
sys_root = '$NDK/platforms/android-31/arch-arm64'
skip_sanity_check = true

[host_machine]
system = 'android'
cpu = 'aarch64'
cpu_family = 'aarch64'
endian = 'little'
CROSS_EOF

BUILD_DIR="$OUT_DIR/build-android"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"
meson setup "$HARFBUZZ_DIR" \
  --cross-file="$ANDROID_CROSS_FILE" \
  --buildtype=release \
  -Ddefault_library=static \
  -Dicu=disabled \
  -Dtests=disabled

meson compile -C "$BUILD_DIR" harfbuzz -j"$(nproc)"

HB_ANDROID_LIB=$(find "$BUILD_DIR" -name "libharfbuzz.a" -print -quit 2>/dev/null || true)
if [ -z "$HB_ANDROID_LIB" ]; then
  echo "[ci_harfbuzz] ERROR: libharfbuzz.a not found in $BUILD_DIR" >&2
  exit 1
fi

cp "$HB_ANDROID_LIB" "$OUT_DIR/libharfbuzz-android-arm64.a"
echo "[ci_harfbuzz] wrote $OUT_DIR/libharfbuzz-android-arm64.a"

# ---- Build for host (for shaping tool verification) ----
echo "[ci_harfbuzz] configuring HarfBuzz for host"
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

HB_HOST_LIB=$(find "$HOST_BUILD_DIR" -name "libharfbuzz.a" -print -quit 2>/dev/null || true)
if [ -z "$HB_HOST_LIB" ]; then
  echo "[ci_harfbuzz] ERROR: host libharfbuzz.a not found in $HOST_BUILD_DIR" >&2
  exit 1
fi

cp "$HB_HOST_LIB" "$OUT_DIR/libharfbuzz-host.a"
echo "[ci_harfbuzz] wrote $OUT_DIR/libharfbuzz-host.a"
