#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
source "$(dirname "$0")/common.sh"
require_macos
bundle="${1:-$RENZU_ROOT/build/RenzuGrade.ofx.bundle}"
mode="${2:-}"
[[ "$mode" == '' || "$mode" == --cpu-only ]] || fail "Unknown mode: $mode"
[[ -d "$bundle/Contents/MacOS" && -d "$bundle/Contents/Resources" ]] || fail "Invalid OFX bundle layout: $bundle"
plist="$bundle/Contents/Info.plist"
binary="$bundle/Contents/MacOS/RenzuGrade.ofx"
[[ -s "$binary" && -s "$plist" ]] || fail "Missing binary or Info.plist"
plutil -lint "$plist"
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$plist")" == RenzuGrade.ofx ]] || fail "Wrong executable in Info.plist"
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist")" == org.renzu.grade ]] || fail "Wrong bundle identifier"
file "$binary"
architectures="$(lipo -archs "$binary")"
[[ "$architectures" == *arm64* || "$architectures" == *x86_64* ]] || fail "No supported architecture"
for arch in $architectures; do
 symbols="$(nm -arch "$arch" -gU "$binary")"
 [[ "$symbols" == *' _OfxGetNumberOfPlugins'* && "$symbols" == *' _OfxGetPlugin'* ]] || fail "Missing OpenFX entry points for $arch"
done
deps="$(otool -L "$binary")"
echo "$deps"
while IFS= read -r dep; do
 [[ "$dep" == /* || "$dep" == @* ]] || continue
 case "$dep" in /System/Library/*|/usr/lib/*) ;; *) fail "Unexpected runtime dependency: $dep";; esac
done < <(otool -L "$binary" | awk '/^\t/ {print $1}')
if [[ "$mode" != --cpu-only ]]; then [[ -s "$bundle/Contents/Resources/renzu_grade.metallib" ]] || fail "Missing compiled Metal shader library"; fi
[[ -z "$(find "$bundle" -type l -print -quit)" ]] || fail "Unexpected symbolic link in bundle"
codesign --verify --strict --verbose=2 "$bundle"
echo "Bundle verified ($architectures)."
