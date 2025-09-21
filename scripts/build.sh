#!/usr/bin/env bash
# Configure, build and (optionally) unit-test SystemPulse.
#
#   scripts/build.sh                 # RelWithDebInfo build + tests
#   scripts/build.sh --debug         # Debug build with AddressSanitizer
#   scripts/build.sh --no-tests      # skip running the unit tests
set -euo pipefail
source "$(dirname "$0")/lib.sh"

build_type="RelWithDebInfo"
run_tests=1
extra_flags=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug)    build_type="Debug"; extra_flags+=("-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer") ;;
    --tsan)     build_type="Debug"; extra_flags+=("-DCMAKE_CXX_FLAGS=-fsanitize=thread") ;;
    --no-tests) run_tests=0 ;;
    --clean)    rm -rf "$SP_BUILD_DIR" ;;
    -h|--help)  sed -n '2,7p' "$0"; exit 0 ;;
    *)          die "unknown flag: $1" ;;
  esac
  shift
done

require_cmd cmake
log "configuring ($build_type) in $SP_BUILD_DIR"
cmake -S "$SP_ROOT" -B "$SP_BUILD_DIR" -DCMAKE_BUILD_TYPE="$build_type" "${extra_flags[@]}"

log "building"
cmake --build "$SP_BUILD_DIR" -j "$(nproc_portable)"

if [[ $run_tests -eq 1 ]]; then
  log "running unit tests"
  (cd "$SP_BUILD_DIR" && ctest --output-on-failure)
fi
log "done: $SP_BUILD_DIR/systempulse"
