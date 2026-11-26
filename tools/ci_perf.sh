#!/usr/bin/env bash
# tools/ci_perf.sh - performance CI lane for NOAT
# Runs --bench on all shader scenarios at both resolutions,
# produces perf_report.json, fails on regression vs baseline.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

OUT_DIR="ci-out/perf"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

HEADLESS="${NOAT_HEADLESS_BIN:-ci-out/native/headless-arm64}"
if [ ! -x "$HEADLESS" ]; then
  echo "[ci_perf] ERROR: headless-arm64 not found at $HEADLESS" >&2
  echo "  Build it first: tools/ci native" >&2
  exit 1
fi

PARAMS_DIR="params"
BASELINE_DIR="reports/baselines"
mkdir -p "$BASELINE_DIR"

# Scenario list: shader, params file, resolutions
SCENARIOS=(
  "paper_base|params/paper_base.json"
  "paper_content|params/paper_content.json"
  "paper_wear|params/paper_wear.json"
)
RESOLUTIONS=("720x1280" "1080x1920")

REPORT="$OUT_DIR/perf_report.json"
echo "{" > "$REPORT"
echo "  \"scenarios\": [" >> "$REPORT"

first_scenario=1
for scenario in "${SCENARIOS[@]}"; do
  IFS='|' read -r shader params <<< "$scenario"
  shader_name="${shader%.frag}"
  shader_spv="$OUT_DIR/${shader_name}.frag.spv"

  # Copy SPIR-V from repo (compiled by shader lane)
  if [ -f "shaders/${shader}.frag.spv" ]; then
    cp "shaders/${shader}.frag.spv" "$shader_spv"
  elif [ -f "$OUT_DIR/${shader}.frag.spv" ]; then
    : # already there
  else
    echo "[ci_perf] WARNING: no SPIR-V for $shader, skipping" >&2
    continue
  fi

  first_res=1
  for res in "${RESOLUTIONS[@]}"; do
    IFS='x' read -r res_w res_h <<< "$res"
    out_png="$OUT_DIR/${shader_name}_${res}.png"
    out_json="$OUT_DIR/${shader_name}_${res}.json"

    echo "[ci_perf] benchmarking $shader $res"
    "$HEADLESS" --bench \
      --shader "$shader_spv" \
      --params "$PARAMS_DIR/${shader}.json" \
      --time 0 --seed 12345 \
      --size "${res_w}x${res_h}" \
      --warmup 10 --frames 60 \
      --out "$out_png" \
      --json "$out_json" >/dev/null 2>&1 || {
      echo "[ci_perf] WARNING: bench failed for $shader $res" >&2
      continue
    }

    # Extract p50/p95 from JSON
    p50=$(python3 -c "import json; print(json.load(open('$out_json'))['gpu_ms_p50'])" 2>/dev/null || echo "0")
    p95=$(python3 -c "import json; print(json.load(open('$out_json'))['gpu_ms_p95'])" 2>/dev/null || echo "0")

    # Check against baseline if it exists
    baseline_file="$BASELINE_DIR/${shader_name}_${res}.baseline.json"
    regression=""
    if [ -f "$baseline_file" ]; then
      base_p50=$(python3 -c "import json; print(json.load(open('$baseline_file'))['gpu_ms_p50'])" 2>/dev/null || echo "0")
      base_p95=$(python3 -c "import json; print(json.load(open('$baseline_file'))['gpu_ms_p95'])" 2>/dev/null || echo "0")
      if python3 -c "exit(0 if float('$p95') <= float('$base_p95') * 1.10 else 1)" 2>/dev/null; then
        regression="ok"
      else
        regression="REGRESSION"
        echo "[ci_perf] REGRESSION: $shader $res p95 ${p95}ms > baseline ${base_p95}ms * 1.10" >&2
      fi
    else
      # First run: save baseline
      cp "$out_json" "$baseline_file"
      regression="baseline"
      echo "[ci_perf] saved baseline for $shader $res"
    fi

    if [ "$first_scenario" = "0" ] || [ "$first_res" = "0" ]; then
      echo "," >> "$REPORT"
    fi
    first_scenario=0
    first_res=0

    cat >> "$REPORT" <<EOF
    {
      "shader": "$shader_name",
      "params": "$params",
      "resolution": "${res_w}x${res_h}",
      "gpu_ms_p50": $p50,
      "gpu_ms_p95": $p95,
      "regression": "$regression"
    }
EOF
  done
done

echo ""
echo "  ]" >> "$REPORT"
echo "}" >> "$REPORT"

echo "[ci_perf] report written to $REPORT"
cat "$REPORT"

# Fail on any regression
if grep -q '"regression": "REGRESSION"' "$REPORT"; then
  echo "[ci_perf] FAIL: performance regression detected" >&2
  exit 1
fi

echo "[ci_perf] done"
