#!/usr/bin/env bash
# Runs a matrix of synthetic workload scenarios under SystemPulse and stores one
# CSV per scenario plus host metadata in results/<timestamp>/.
#
#   scripts/run_benchmark.sh                       # all scenarios, defaults
#   scripts/run_benchmark.sh -s "cpu io" -w 8 -d 60
#   scripts/run_benchmark.sh -i 250 --report       # faster sampling, then report
#
# Options:
#   -s SCENARIOS   space-separated subset of: idle cpu mem io mixed (default: all)
#   -w WORKERS     worker processes per scenario (default: number of CPUs / 2, min 2)
#   -d SECONDS     duration of each scenario (default 30)
#   -i MS          sampling interval in ms (default 500)
#   -o DIR         output directory (default results/<timestamp>)
#   --cooldown S   idle seconds between scenarios (default 5)
#   --report       run scripts/report.sh on the results afterwards
set -euo pipefail
source "$(dirname "$0")/lib.sh"

scenarios="idle cpu mem io mixed"
cpus="$(nproc_portable)"
workers=$(( cpus / 2 )); (( workers < 2 )) && workers=2
duration=30
interval=500
outdir=""
cooldown=5
do_report=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    -s) scenarios="$2"; shift ;;
    -w) workers="$2"; shift ;;
    -d) duration="$2"; shift ;;
    -i) interval="$2"; shift ;;
    -o) outdir="$2"; shift ;;
    --cooldown) cooldown="$2"; shift ;;
    --report) do_report=1 ;;
    -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
    *) die "unknown flag: $1" ;;
  esac
  shift
done

require_linux
require_binaries
outdir="${outdir:-$SP_ROOT/results/$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$outdir"
io_dir="$(mktemp -d "${TMPDIR:-/tmp}/systempulse-io.XXXXXX")"
trap 'rm -rf "$io_dir"' EXIT

write_host_metadata "$outdir/host.json" \
  "\"workers\": $workers, \"duration_sec\": $duration, \"interval_ms\": $interval, \"scenarios\": \"$scenarios\""

worker_cmd_for() {
  local mode="$1"
  local base="$SP_BUILD_DIR/pulse_worker --duration $duration --io-dir $io_dir"
  case "$mode" in
    cpu)   echo "$base --mode cpu --threads 1 --cpu-load 90" ;;
    mem)   echo "$base --mode mem --mem-mb 256" ;;
    io)    echo "$base --mode io --io-mb 64" ;;
    mixed) echo "$base --mode mixed --threads 2" ;;
    *)     die "no worker command for scenario $mode" ;;
  esac
}

log "results -> $outdir  (workers=$workers duration=${duration}s interval=${interval}ms)"
first=1
for scenario in $scenarios; do
  if [[ $first -eq 0 && $cooldown -gt 0 ]]; then
    log "cooling down for ${cooldown}s"
    sleep "$cooldown"
  fi
  first=0

  csv="$outdir/$scenario.csv"
  logfile="$outdir/$scenario.log"
  log "scenario '$scenario' -> $(basename "$csv")"
  start_ts=$(date +%s)

  if [[ "$scenario" == "idle" ]]; then
    "$SP_BUILD_DIR/systempulse" -q -i "$interval" -d "$duration" -o "$csv" 2>"$logfile"
  else
    # Give the monitor a short tail after workers exit to capture the ramp-down.
    "$SP_BUILD_DIR/systempulse" -q -i "$interval" -d "$(( duration + 3 ))" -o "$csv" \
      --spawn "$workers" --worker-cmd "$(worker_cmd_for "$scenario")" \
      --exit-with-workers 2>"$logfile"
  fi

  rows=$(( $(wc -l <"$csv") - 1 ))
  log "  done in $(( $(date +%s) - start_ts ))s, $rows samples"
  echo "$scenario,$csv,$rows" >>"$outdir/index.csv"
done

log "benchmark complete: $outdir"
if [[ $do_report -eq 1 ]]; then
  "$SP_ROOT/scripts/report.sh" "$outdir"
fi
