#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
source "$(dirname "$0")/common.sh"
[[ "$(uname -s)" == Linux && "$(uname -m)" == x86_64 ]] || fail "This script requires Linux x86_64."
config=Release
build_dir="${RENZU_BUILD_DIR:-$RENZU_ROOT/build-linux}"
run_tests=0
package=0
sanitize=OFF
while [[ $# -gt 0 ]]; do
 case "$1" in
  --debug) config=Debug; shift;;
  --release) config=Release; shift;;
  --build-dir) [[ $# -ge 2 ]] || fail "--build-dir needs a directory"; build_dir="$2"; shift 2;;
  --test) run_tests=1; shift;;
  --package) package=1; run_tests=1; shift;;
  --sanitize) sanitize=ON; shift;;
  *) fail "Unknown option: $1";;
 esac
done
if [[ "$package" == 1 ]]; then
 [[ "$config" == Release && "$sanitize" == OFF ]] || fail "Distribution packages require Release without sanitizers."
fi
for tool in cmake make ctest cpack; do require_tool "$tool"; done
cmake -S "$RENZU_ROOT" -B "$build_dir" -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE="$config" \
 -DRENZU_ENABLE_METAL=OFF -DRENZU_BUILD_TESTS=ON -DRENZU_SANITIZE="$sanitize" \
 -DOFX_SDK_ROOT="${OFX_SDK_ROOT:-$RENZU_ROOT/third_party/ofx}"
cmake --build "$build_dir" --parallel
if [[ "$run_tests" == 1 ]]; then
 ctest --test-dir "$build_dir" --output-on-failure --no-tests=error
fi
if [[ "$package" == 1 ]]; then
 cpack --config "$build_dir/CPackConfig.cmake"
fi
