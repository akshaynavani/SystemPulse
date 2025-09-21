#!/usr/bin/env bash
# Shared helpers for SystemPulse scripts. Source this file; don't execute it.

SP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SP_BUILD_DIR="${SP_BUILD_DIR:-$SP_ROOT/build}"

if [[ -t 2 ]]; then
  _c_info=$'\033[1;34m'; _c_warn=$'\033[1;33m'; _c_err=$'\033[1;31m'; _c_off=$'\033[0m'
else
  _c_info=""; _c_warn=""; _c_err=""; _c_off=""
fi

log()  { printf '%s[%s]%s %s\n' "$_c_info" "$(date +%H:%M:%S)" "$_c_off" "$*" >&2; }
warn() { printf '%s[warn]%s %s\n' "$_c_warn" "$_c_off" "$*" >&2; }
die()  { printf '%s[error]%s %s\n' "$_c_err" "$_c_off" "$*" >&2; exit 1; }

require_cmd() {
  local c
  for c in "$@"; do
    command -v "$c" >/dev/null 2>&1 || die "required command not found: $c"
  done
}

require_linux() {
  [[ "$(uname -s)" == "Linux" ]] || die "SystemPulse reads /proc and must run on Linux"
}

require_binaries() {
  [[ -x "$SP_BUILD_DIR/systempulse" && -x "$SP_BUILD_DIR/pulse_worker" ]] ||
    die "binaries not found in $SP_BUILD_DIR — run scripts/build.sh first"
}

nproc_portable() {
  getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1
}

# Writes simple host metadata as JSON to the given path.
write_host_metadata() {
  local out="$1"; shift
  local extra="${1:-}"
  cat >"$out" <<JSON
{
  "hostname": "$(hostname)",
  "kernel": "$(uname -r)",
  "arch": "$(uname -m)",
  "cpus": $(nproc_portable),
  "mem_total_kb": $(awk '/^MemTotal:/ {print $2}' /proc/meminfo 2>/dev/null || echo 0),
  "started_at": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"${extra:+,
  $extra}
}
JSON
}
