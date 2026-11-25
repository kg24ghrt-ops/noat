#!/usr/bin/env bash
# tools/ci_shader.sh - shader CI lane: GLSL -> SPIR-V, validate, malioc best-effort
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

OUT_DIR="ci-out/shader"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

# Install glslang (provides glslangValidator) if missing.
if ! command -v glslangValidator >/dev/null 2>&1; then
  echo "[ci_shader] installing glslang-tools"
  sudo apt-get update -y
  sudo apt-get install -y glslang-tools
fi

REPORT="$OUT_DIR/report.json"
echo "{" > "$REPORT"
echo "  \"shaders\": [" >> "$REPORT"

first=1
for frag in shaders/*.frag; do
  [ -f "$frag" ] || continue
  name="$(basename "$frag" .frag)"
  spv="$OUT_DIR/$name.frag.spv"
  echo "[ci_shader] compiling $frag -> $spv"
  if glslangValidator -V "$frag" -o "$spv"; then
    status="ok"
  else
    status="fail"
    echo "[ci_shader] ERROR: glslangValidator failed for $frag" >&2
  fi
  # spirv-val validation (best-effort; glslangValidator already validates)
  val_status="skipped"
  if command -v spirv-val >/dev/null 2>&1; then
    if spirv-val "$spv" >/dev/null 2>&1; then
      val_status="ok"
    else
      val_status="fail"
    fi
  fi
  # malioc best-effort (may not be available on x86 runner)
  malioc_status="skipped"
  malioc_cycles=-1
  if command -v malioc >/dev/null 2>&1; then
    if out=$(malioc -c Mali-G610 "$spv" 2>&1); then
      malioc_status="ok"
    else
      malioc_status="fail"
    fi
  fi
  if [ "$first" = "1" ]; then first=0; else echo "," >> "$REPORT"; fi
  cat >> "$REPORT" <<EOF
    {
      "name": "$name",
      "file": "$frag",
      "spirv": "$spv",
      "compile": "$status",
      "validation": "$val_status",
      "malioc": "$malioc_status",
      "malioc_cycles": $malioc_cycles
    }
EOF
done

echo "  ]" >> "$REPORT"
echo "}" >> "$REPORT"

echo "[ci_shader] report:"
cat "$REPORT"

# Fail if any shader failed to compile.
if grep -q '"compile": "fail"' "$REPORT"; then
  echo "[ci_shader] FAIL: shader compilation error" >&2
  exit 1
fi

echo "[ci_shader] done"
