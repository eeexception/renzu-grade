#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
source "$(dirname "$0")/common.sh"
require_macos
bundle="${1:-$RENZU_ROOT/build/RenzuGrade.ofx.bundle}"
mode="${2:-}"
"$RENZU_ROOT/scripts/verify_bundle.sh" "$bundle" "$mode"
version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$bundle/Contents/Info.plist")"
arch="$(lipo -archs "$bundle/Contents/MacOS/RenzuGrade.ofx" | tr ' ' '-')"
suffix=''; [[ "$mode" != --cpu-only ]] || suffix=-cpu-only
name="renzu-grade-$version-macos-$arch$suffix"
mkdir -p "$RENZU_ROOT/dist"
stage="$(mktemp -d "$RENZU_ROOT/dist/.package-XXXXXX")"
# Only the temporary staging tree created above is cleaned.
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/$name/scripts" "$stage/$name/docs"
ditto --norsrc --noextattr --noqtn "$bundle" "$stage/$name/RenzuGrade.ofx.bundle"
cp "$RENZU_ROOT/LICENSE" "$RENZU_ROOT/NOTICE" "$RENZU_ROOT/README.md" "$stage/$name/"
cp "$RENZU_ROOT/CONTRIBUTING.md" "$RENZU_ROOT/CHANGELOG.md" "$stage/$name/"
cp "$RENZU_ROOT/third_party/ofx/LICENSE.md" "$stage/$name/OPENFX-LICENSE.md"
cp "$RENZU_ROOT/scripts/common.sh" "$RENZU_ROOT/scripts/install.sh" "$RENZU_ROOT/scripts/uninstall.sh" "$RENZU_ROOT/scripts/verify_bundle.sh" "$stage/$name/scripts/"
cp "$RENZU_ROOT"/docs/*.md "$stage/$name/docs/"
# Accompany the binary with the corresponding project source and build scripts.
# Copy the working tree (not only HEAD) so staged development builds include their source.
source_dir="$stage/$name/Source/renzu-grade"
mkdir -p "$source_dir"
for directory in cmake include src shaders tests scripts docs third_party/ofx .github; do
 ditto --norsrc --noextattr --noqtn "$RENZU_ROOT/$directory" "$source_dir/$directory"
done
for source_file in CMakeLists.txt LICENSE NOTICE README.md CONTRIBUTING.md CHANGELOG.md .gitignore .clang-format; do
 cp "$RENZU_ROOT/$source_file" "$source_dir/"
done
cat > "$stage/$name/INSTALL.txt" <<TEXT
Renzu Grade $version
From this directory run:
  ./scripts/install.sh ./RenzuGrade.ofx.bundle $mode
Restart DaVinci Resolve. Use docs/manual-test-plan.md for validation.
Uninstall: ./scripts/uninstall.sh
License: GNU GPL version 3 only. Corresponding source and build instructions:
  Source/renzu-grade/README.md
This development build is ad-hoc signed, not notarized. See README.md.
TEXT
(cd "$stage/$name" && find RenzuGrade.ofx.bundle -type f -exec shasum -a 256 {} \; > SHA256SUMS)
(cd "$stage/$name" && shasum -a 256 -c SHA256SUMS)
archive="$RENZU_ROOT/dist/$name.zip"
[[ ! -e "$archive" ]] || fail "Archive exists; move it aside before packaging: $archive"
ditto -c -k --norsrc --noextattr --noqtn --keepParent "$stage/$name" "$archive"
unzip -t "$archive"
shasum -a 256 "$archive" > "$archive.sha256"
echo "Created $archive; nothing uploaded."
