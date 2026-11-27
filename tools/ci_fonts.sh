#!/usr/bin/env bash
# tools/ci_fonts.sh - Fetch pinned OFL font assets in CI only.
# Downloads Noto Sans Myanmar (VF) and Patrick Hand into
# app/src/main/assets/fonts/, verifies they are valid TrueType, then cleans
# temporary downloads. Never committed; never placed in /sdcard.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

OUT_DIR="ci-out/fonts"
ASSETS_DIR="app/src/main/assets/fonts"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR" "$ASSETS_DIR"

# Pinned font sources (google/fonts raw, SIL OFL).
# Noto Sans Myanmar VF — complex-script shaping reference + Myanmar SDF atlas.
declare -A FONTS=(
  ["NotoSansMyanmar-VF.ttf"]="https://github.com/google/fonts/raw/main/ofl/notosansmyanmar/NotoSansMyanmar%5Bwdth%2Cwght%5D.ttf"
  ["PatrickHand-Regular.ttf"]="https://github.com/google/fonts/raw/main/ofl/patrickhand/PatrickHand-Regular.ttf"
)

for name in "${!FONTS[@]}"; do
  url="${FONTS[$name]}"
  tmp="$OUT_DIR/$name.part"
  final="$OUT_DIR/$name"
  echo "[ci_fonts] fetching $name"
  curl -fsSL --retry 3 --connect-timeout 30 -o "$tmp" "$url"
  # Verify it is a TrueType font (magic 0x00010000 or 'true'/'typ1').
  if ! head -c 4 "$tmp" | od -An -tx1 | grep -qE '00 01 00 00|74 72 75 65|74 79 70 31'; then
    echo "[ci_fonts] ERROR: $name is not a valid TrueType font" >&2
    rm -f "$tmp"
    exit 1
  fi
  size=$(stat -c%s "$tmp")
  if [ "$size" -lt 10000 ]; then
    echo "[ci_fonts] ERROR: $name too small ($size bytes)" >&2
    rm -f "$tmp"
    exit 1
  fi
  mv "$tmp" "$final"
  echo "[ci_fonts] wrote $final ($size bytes)"
done

# Stage into the app assets directory (overwrite any stale copies).
cp -f "$OUT_DIR"/*.ttf "$ASSETS_DIR/"
echo "[ci_fonts] staged fonts into $ASSETS_DIR"
ls -la "$ASSETS_DIR"

# Clean temporary downloads; never leave executables or large assets in /sdcard.
rm -rf "$OUT_DIR"
echo "[ci_fonts] done (temp cleaned)"
