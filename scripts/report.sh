#!/usr/bin/env bash
# Summarises SystemPulse CSVs. Prints a quick awk-based table for each file and,
# if Python dependencies are available, runs the full time-series analysis.
#
#   scripts/report.sh results/20250101-120000        # every *.csv in the dir
#   scripts/report.sh run.csv                        # single file
#   scripts/report.sh --no-python results/<ts>
set -euo pipefail
source "$(dirname "$0")/lib.sh"

use_python=1
if [[ "${1:-}" == "--no-python" ]]; then use_python=0; shift; fi
[[ $# -ge 1 ]] || die "usage: $0 [--no-python] <results-dir|file.csv>"
target="$1"

csvs=()
if [[ -d "$target" ]]; then
  while IFS= read -r f; do csvs+=("$f"); done < <(find "$target" -maxdepth 1 -name '*.csv' ! -name 'index.csv' | sort)
  report_dir="$target/report"
else
  [[ -f "$target" ]] || die "no such file: $target"
  csvs=("$target")
  report_dir="$(dirname "$target")/report"
fi
[[ ${#csvs[@]} -gt 0 ]] || die "no CSV files found in $target"
mkdir -p "$report_dir"

# Per-series count/mean/min/max for the headline metrics. Entities are quoted in
# CSV only when they contain commas, which never applies to these series.
summarise() {
  awk -F',' '
    NR == 1 { next }
    {
      key = $2 "/" $3 "/" $4
      if (key !~ /^(cpu\/all\/busy_pct|cpu\/all\/iowait_pct|mem\/system\/used_pct|load\/system\/load1|sys\/system\/ctx_switches_per_sec|proc\/workers\/(cpu_pct|rss_kb|disk_read_bps|disk_write_bps)|io\/[^/]+\/(util_pct|read_bps|write_bps|await_ms))$/) next
      v = $5 + 0
      n[key]++; s[key] += v
      if (!(key in mn) || v < mn[key]) mn[key] = v
      if (!(key in mx) || v > mx[key]) mx[key] = v
      if (first == "" || $1 < first) first = $1
      if ($1 > last) last = $1
    }
    END {
      printf "  span: %.1fs\n", (last - first) / 1000.0
      printf "  %-38s %8s %14s %14s %14s\n", "series", "n", "mean", "min", "max"
      cmd = "sort"
      for (k in n) printf "  %-38s %8d %14.2f %14.2f %14.2f\n", k, n[k], s[k] / n[k], mn[k], mx[k] | cmd
      close(cmd)
    }' "$1"
}

summary="$report_dir/summary.txt"
: >"$summary"
for csv in "${csvs[@]}"; do
  {
    echo "== $(basename "$csv" .csv) =="
    summarise "$csv"
    echo
  } | tee -a "$summary"
done
log "text summary written to $summary"

if [[ $use_python -eq 1 ]]; then
  py="${PYTHON:-python3}"
  if command -v "$py" >/dev/null 2>&1 &&
     PYTHONPATH="$SP_ROOT/analysis${PYTHONPATH:+:$PYTHONPATH}" "$py" -c 'import pandas, matplotlib, systempulse_analysis' 2>/dev/null; then
    log "running Python analysis pipeline"
    PYTHONPATH="$SP_ROOT/analysis${PYTHONPATH:+:$PYTHONPATH}" \
      "$py" -m systempulse_analysis "${csvs[@]}" --out "$report_dir"
  else
    warn "pandas/matplotlib not available; skipping Python analysis (pip install -r analysis/requirements.txt)"
  fi
fi
