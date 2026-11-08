#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
source "$(dirname "$0")/common.sh"
require_macos
printf 'Architecture: %s\n' "$(uname -m)"
for tool in xcrun xcodebuild clang++ cmake ninja git; do require_tool "$tool"; done
xcode-select -p
xcrun --sdk macosx clang++ --version
xcrun --sdk macosx metal --version || fail "Install the Metal toolchain: xcodebuild -downloadComponent MetalToolchain"
xcrun --sdk macosx --find metallib
sdk="${OFX_SDK_ROOT:-$RENZU_ROOT/third_party/ofx}"
[[ -f "$sdk/include/ofxImageEffect.h" && -f "$sdk/include/ofxGPURender.h" ]] || fail "Set OFX_SDK_ROOT to an official OpenFX SDK checkout. A pinned header subset is shipped in third_party/ofx."
echo "Prerequisites available; no system configuration changed."
