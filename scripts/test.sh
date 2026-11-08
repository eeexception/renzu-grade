#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
source "$(dirname "$0")/common.sh"
for arg in "$@"; do [[ "$arg" != --without-tests ]] || fail "Use build.sh for a build without tests."; done
"$RENZU_ROOT/scripts/build.sh" "$@"
build_dir="${RENZU_BUILD_DIR:-$RENZU_ROOT/build}"
while [[ $# -gt 0 ]]; do case "$1" in --build-dir) build_dir="$2"; shift 2;; *) shift;; esac; done
ctest --test-dir "$build_dir" --output-on-failure --no-tests=error
