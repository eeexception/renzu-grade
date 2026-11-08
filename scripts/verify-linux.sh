#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
source "$(dirname "$0")/common.sh"
[[ "$(uname -s)" == Linux ]] || fail "This operation requires Linux."
bundle="${1:-$RENZU_ROOT/build-linux/RenzuGrade.ofx.bundle}"
max_glibc="${2:-}"
binary="$bundle/Contents/Linux-x86-64/RenzuGrade.ofx"
[[ -s "$binary" ]] || fail "Missing Linux OpenFX binary: $binary"
for tool in readelf nm sort; do require_tool "$tool"; done
header="$(LC_ALL=C readelf -h "$binary")"
[[ "$header" == *ELF64* && "$header" == *'Advanced Micro Devices X86-64'* ]] || fail "Expected an x86_64 ELF binary."
symbols="$(nm -D --defined-only "$binary" | awk '{print $3}' | sort)"
[[ "$symbols" == $'OfxGetNumberOfPlugins\nOfxGetPlugin' ]] || fail "Unexpected exported symbols: $symbols"
dependencies="$(readelf -d "$binary" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')"
while IFS= read -r dependency; do
 case "$dependency" in
  libc.so.6|libm.so.6|libpthread.so.0|librt.so.1|libdl.so.2|ld-linux-x86-64.so.2) ;;
  *) fail "Unexpected runtime dependency: $dependency";;
 esac
done <<< "$dependencies"
versions="$(readelf --version-info "$binary")"
required_glibc="$(printf '%s\n' "$versions" | sed -n 's/.*Name: GLIBC_\([0-9.]*\).*/\1/p' | sort -Vu | tail -1)"
[[ -n "$required_glibc" ]] || fail "Cannot determine glibc requirement."
if [[ -n "$max_glibc" ]]; then
 highest="$(printf '%s\n' "$required_glibc" "$max_glibc" | sort -V | tail -1)"
 [[ "$highest" == "$max_glibc" ]] || fail "Requires glibc $required_glibc, above distribution baseline $max_glibc."
fi
[[ -z "$(find "$bundle" -type l -print -quit)" ]] || fail "Unexpected symbolic link in bundle."
echo "Linux x86_64 CPU bundle verified; requires glibc >= $required_glibc."
echo "$dependencies"
