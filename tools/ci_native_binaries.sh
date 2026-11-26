#!/usr/bin/env bash
# tools/ci_native_binaries.sh - One-time build of prebuilt libpaper-engine.so
# for Android ABIs. Produces jniLibs-ready binaries plus metadata.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

OUT_DIR="ci-out/native-binaries"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

# Locate Android NDK.
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${ANDROID_NDK:-}}}"
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
  echo "[ci_native_binaries] ERROR: ANDROID_NDK_ROOT not set" >&2
  exit 1
fi
echo "[ci_native_binaries] NDK: $NDK"

TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake"
if [ ! -f "$TOOLCHAIN_FILE" ]; then
  echo "[ci_native_binaries] ERROR: NDK toolchain file not found at $TOOLCHAIN_FILE" >&2
  exit 1
fi

# Source commit for provenance.
SOURCE_COMMIT="$(git rev-parse HEAD)"
echo "[ci_native_binaries] Source commit: $SOURCE_COMMIT"

# Metadata output.
METADATA_FILE="$OUT_DIR/binaries-metadata.json"
echo "{" > "$METADATA_FILE"
echo "  \"source_commit\": \"$SOURCE_COMMIT\"," >> "$METADATA_FILE"
echo "  \"ndk_version\": \"30.0.16248370\"," >> "$METADATA_FILE"
echo "  \"cmake_version\": \"3.31.6\"," >> "$METADATA_FILE"
echo "  \"build_type\": \"Release\"," >> "$METADATA_FILE"
echo "  \"binaries\": [" >> "$METADATA_FILE"

# Build for each ABI.
FIRST=true
for ABI in arm64-v8a x86_64; do
  echo "[ci_native_binaries] Building for $ABI"

  BUILD_DIR="$OUT_DIR/build-$ABI"
  rm -rf "$BUILD_DIR"
  mkdir -p "$BUILD_DIR"

  cmake -S app/src/main/cpp -B "$BUILD_DIR/build" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM=android-31 \
    -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" \
    -DCMAKE_SHARED_LINKER_FLAGS_RELEASE="-s" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    2>&1 | tail -n 30

  cmake --build "$BUILD_DIR/build" --config Release -j"$(nproc)"

  SO_FILE="$BUILD_DIR/build/libpaper-engine.so"
  if [ ! -f "$SO_FILE" ]; then
    echo "[ci_native_binaries] ERROR: libpaper-engine.so not found for $ABI" >&2
    exit 1
  fi

  # Validate architecture.
  echo "[ci_native_binaries] Validating $ABI binary:"
  file "$SO_FILE"

  # Strip debug symbols.
  strip --strip-all "$SO_FILE" 2>/dev/null || true

  # Validate JNI exports.
  echo "[ci_native_binaries] Validating JNI exports for $ABI:"
  $NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm --defined-only "$SO_FILE" | grep -i "Java_" | head -20 || true

  # Compute SHA-256.
  SHA256="$(sha256sum "$SO_FILE" | awk '{print $1}')"
  echo "[ci_native_binaries] SHA-256 ($ABI): $SHA256"

  # Copy to jniLibs structure.
  JNI_DIR="$OUT_DIR/jniLibs/$ABI"
  mkdir -p "$JNI_DIR"
  cp "$SO_FILE" "$JNI_DIR/libpaper-engine.so"

  # Append metadata.
  if [ "$FIRST" = true ]; then
    FIRST=false
  else
    echo "," >> "$METADATA_FILE"
  fi
  echo "    {" >> "$METADATA_FILE"
  echo "      \"abi\": \"$ABI\"," >> "$METADATA_FILE"
  echo "      \"path\": \"jniLibs/$ABI/libpaper-engine.so\"," >> "$METADATA_FILE"
  echo "      \"sha256\": \"$SHA256\"" >> "$METADATA_FILE"
  echo "    }" >> "$METADATA_FILE"
done

echo "  ]" >> "$METADATA_FILE"
echo "}" >> "$METADATA_FILE"

echo "[ci_native_binaries] Done. Binaries in $OUT_DIR/jniLibs/"
ls -laR "$OUT_DIR/jniLibs/"
