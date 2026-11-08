#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
source "$(dirname "$0")/common.sh"
require_macos
target=/Library/OFX/Plugins/RenzuGrade.ofx.bundle
[[ ! -L "$target" ]] || fail "Refusing to remove a symbolic link"
if [[ ! -e "$target" ]]; then echo "Renzu Grade is not installed."; exit 0; fi
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$target/Contents/Info.plist")" == org.renzu.grade ]] || fail "Bundle identifier does not match Renzu Grade"
# Move outside the scanned Plugins tree. This safely uninstalls and retains recovery.
backup="/Library/OFX/RenzuGrade-Backups/uninstalled-$(date +%Y%m%d-%H%M%S)-$$"
renzu_use_sudo=0; if [[ ! -w "$(dirname "$target")" ]]; then renzu_use_sudo=1; fi
system_write mkdir -p "$backup"
system_write mv "$target" "$backup/RenzuGrade.ofx.bundle"
echo "Uninstalled Renzu Grade. Recovery copy: $backup/RenzuGrade.ofx.bundle"
echo "Quit and restart DaVinci Resolve. Other plugins were not changed."
