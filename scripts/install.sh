#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
source "$(dirname "$0")/common.sh"
require_macos
bundle="${1:-$RENZU_ROOT/build/RenzuGrade.ofx.bundle}"
mode="${2:-}"
"$RENZU_ROOT/scripts/verify_bundle.sh" "$bundle" "$mode"
destination=/Library/OFX/Plugins
staging="$destination/.RenzuGrade-install-$$.ofx.bundle"
target="$destination/RenzuGrade.ofx.bundle"
[[ ! -L "$destination" && ! -L "$target" ]] || fail "Refusing an installation destination that is a symbolic link"
renzu_use_sudo=0; if [[ ! -w "$destination" ]]; then renzu_use_sudo=1; fi
system_write mkdir -p "$destination"
[[ ! -e "$staging" ]] || fail "Staging path already exists"
system_write ditto "$bundle" "$staging"
"$RENZU_ROOT/scripts/verify_bundle.sh" "$staging" "$mode"
backup=''
if [[ -e "$target" ]]; then
 backup="/Library/OFX/RenzuGrade-Backups/$(date +%Y%m%d-%H%M%S)-$$/RenzuGrade.ofx.bundle"
 system_write mkdir -p "$(dirname "$backup")"
 system_write mv "$target" "$backup"
 echo "Previous version preserved: $backup"
fi
if ! system_write mv "$staging" "$target"; then
 if [[ -n "$backup" && ! -e "$target" ]]; then
  system_write mv "$backup" "$target"
 fi
 fail "Could not activate staged bundle; previous installation restored if present."
fi
echo "Installed $target. Quit and restart DaVinci Resolve."
