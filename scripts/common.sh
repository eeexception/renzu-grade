#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
RENZU_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fail() { echo "Error: $*" >&2; exit 1; }
require_macos() { [[ "$(uname -s)" == Darwin ]] || fail "This operation requires macOS."; }
require_tool() { command -v "$1" >/dev/null 2>&1 || fail "Missing prerequisite: $1"; }
# Avoid empty command-prefix arrays: macOS Bash 3.2 rejects them under nounset.
system_write() {
 if [[ "${renzu_use_sudo:-0}" == 1 ]]; then sudo "$@"; else "$@"; fi
}
