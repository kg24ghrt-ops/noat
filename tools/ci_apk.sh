#!/usr/bin/env bash
# tools/ci_apk.sh - APK CI lane: Gradle assembleDebug only
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

if [ ! -f gradlew ]; then
  echo "[ci_apk] ERROR: gradlew not found" >&2
  exit 1
fi

echo "[ci_apk] assembling debug APK"

# Fetch pinned font assets in CI only (bundled into the APK via assets/).
bash "$REPO_ROOT/tools/ci_fonts.sh"

./gradlew --no-daemon assembleDebug

OUT_DIR="ci-out/apk"
mkdir -p "$OUT_DIR"
cp -r app/build/outputs/apk/debug "$OUT_DIR/" 2>/dev/null || true
ls -la "$OUT_DIR/debug" 2>/dev/null || true
echo "[ci_apk] done"
