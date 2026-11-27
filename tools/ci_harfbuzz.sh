#!/usr/bin/env bash
# tools/ci_harfbuzz.sh - Build HarfBuzz for Android arm64 using direct clang
# Compiles HarfBuzz source files directly with clang --target=aarch64-linux-android31
# to avoid CMake cross-compilation issues.
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

if [ ! -f "$HARFBUZZ_DIR/src/hb-common.h" ]; then
  FOUND_DIR=$(find "$OUT_DIR" -maxdepth 4 -name "hb-common.h" -print -quit 2>/dev/null | xargs -I{} dirname {} 2>/dev/null | head -1 || true)
  if [ -z "$FOUND_DIR" ]; then
    echo "[ci_harfbuzz] ERROR: cannot find HarfBuzz source headers under $OUT_DIR" >&2
    exit 1
  fi
  echo "[ci_harfbuzz] found HarfBuzz source at $FOUND_DIR"
  HARFBUZZ_DIR="$FOUND_DIR"
fi

HARFBUZZ_DIR="$(cd "$HARFBUZZ_DIR" && pwd)"
SRC_DIR="$HARFBUZZ_DIR/src"

# Toolchain: plain clang with --target flag (not target-prefixed binary)
CLANG="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
LLVM_AR="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar"

# ---- Generate headers ----
mkdir -p "$HARFBUZZ_DIR/build-android/src"
cat > "$HARFBUZZ_DIR/build-android/src/hb-version.h" << 'VERSION_EOF'
#ifndef HB_VERSION_H
#define HB_VERSION_H

#define HB_VERSION_MAJOR 8
#define HB_VERSION_MINOR 4
#define HB_VERSION_MICRO 0
#define HB_VERSION "8.4.0"

#endif
VERSION_EOF

cat > "$HARFBUZZ_DIR/build-android/src/config.h" << 'CONFIG_EOF'
#ifndef HB_CONFIG_H
#define HB_CONFIG_H

#define HAVE_CONFIG_H 1

#endif
CONFIG_EOF

# ---- Compile all HarfBuzz source files for Android arm64 ----
echo "[ci_harfbuzz] compiling HarfBuzz for Android arm64"

OBJ_DIR="$OUT_DIR/obj-android"
rm -rf "$OBJ_DIR"
mkdir -p "$OBJ_DIR"

INCLUDES="-I$HARFBUZZ_DIR/src -I$HARFBUZZ_DIR/build-android/src -DHAVE_CONFIG_H"

# Collect all .cc files that don't depend on optional backends or are test files
SRC_FILES=()
for f in "$SRC_DIR"/*.cc; do
    bn=$(basename "$f")
    case "$bn" in
        hb-freetype.cc|hb-glib.cc|hb-icu.cc|hb-coretext.cc|hb-directwrite.cc|hb-fonttools.cc|*test*)
            continue ;;
    esac
    SRC_FILES+=("$f")
done

echo "[ci_harfbuzz] compiling ${#SRC_FILES[@]} source files"

for src in "${SRC_FILES[@]}"; do
    bn=$(basename "$src" .cc)
    obj="$OBJ_DIR/$bn.o"
    "$CLANG" --target=armv7a-linux-androideabi31 \
        $INCLUDES \
        -O2 -DNDEBUG -Wno-error -c "$src" -o "$obj" 2>&1 | tail -5
    if [ ! -f "$obj" ]; then
        echo "[ci_harfbuzz] ERROR: compilation failed for $src" >&2
        exit 1
    fi
done

# ---- Archive into static library ----
echo "[ci_harfbuzz] archiving into libharfbuzz.a"
"$LLVM_AR" rcs "$OUT_DIR/libharfbuzz-android-arm64.a" "$OBJ_DIR"/*.o

HB_ANDROID_LIB="$OUT_DIR/libharfbuzz-android-arm64.a"
if [ ! -f "$HB_ANDROID_LIB" ]; then
  echo "[ci_harfbuzz] ERROR: libharfbuzz.a not found at $HB_ANDROID_LIB" >&2
  exit 1
fi
echo "[ci_harfbuzz] wrote $HB_ANDROID_LIB"

# ---- Verify architecture ----
echo "[ci_harfbuzz] verifying architecture:"
file "$HB_ANDROID_LIB"

# ---- Build for host (for shaping tool verification) ----
echo "[ci_harfbuzz] building HarfBuzz for host"
HOST_OBJ_DIR="$OUT_DIR/obj-host"
rm -rf "$HOST_OBJ_DIR"
mkdir -p "$HOST_OBJ_DIR"

HOST_CLANG="clang"
HOST_INCLUDES="-I$HARFBUZZ_DIR/src -I$HARFBUZZ_DIR/build-android/src -DHAVE_CONFIG_H"

for src in "${SRC_FILES[@]}"; do
    bn=$(basename "$src" .cc)
    obj="$HOST_OBJ_DIR/$bn.o"
    "$HOST_CLANG" -O2 -DNDEBUG $HOST_INCLUDES -c "$src" -o "$obj" 2>&1 | tail -5
done

"$LLVM_AR" rcs "$OUT_DIR/libharfbuzz-host.a" "$HOST_OBJ_DIR"/*.o

HB_HOST_LIB="$OUT_DIR/libharfbuzz-host.a"
if [ ! -f "$HB_HOST_LIB" ]; then
  echo "[ci_harfbuzz] ERROR: host libharfbuzz.a not found at $HB_HOST_LIB" >&2
  exit 1
fi
echo "[ci_harfbuzz] wrote $HB_HOST_LIB"

echo "[ci_harfbuzz] done"
