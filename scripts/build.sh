#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
source "$(dirname "$0")/common.sh"
require_macos
config=Release
arch="$(uname -m)"
metal=ON
build_tests=ON
build_dir="${RENZU_BUILD_DIR:-$RENZU_ROOT/build}"
while [[ $# -gt 0 ]]; do
 case "$1" in
  --debug) config=Debug; shift;;
  --release) config=Release; shift;;
  --arch) [[ $# -ge 2 ]] || fail "--arch needs arm64, x86_64, or universal"; arch="$2"; shift 2;;
  --build-dir) [[ $# -ge 2 ]] || fail "--build-dir needs a directory"; build_dir="$2"; shift 2;;
  --cpu-only) metal=OFF; shift;;
  --without-tests) build_tests=OFF; shift;;
  *) fail "Unknown option: $1";;
 esac
done
case "$arch" in arm64|x86_64) ;; universal) arch='arm64;x86_64';; *) fail "Unsupported architecture: $arch";; esac
for tool in cmake ninja xcrun; do require_tool "$tool"; done
cmake -S "$RENZU_ROOT" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE="$config" -DCMAKE_OSX_ARCHITECTURES="$arch" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DRENZU_ENABLE_METAL="$metal" -DRENZU_BUILD_TESTS="$build_tests" -DOFX_SDK_ROOT="${OFX_SDK_ROOT:-$RENZU_ROOT/third_party/ofx}"
cmake --build "$build_dir" --parallel
if [[ "$metal" == ON ]]; then "$RENZU_ROOT/scripts/verify_bundle.sh" "$build_dir/RenzuGrade.ofx.bundle"; else "$RENZU_ROOT/scripts/verify_bundle.sh" "$build_dir/RenzuGrade.ofx.bundle" --cpu-only; fi
