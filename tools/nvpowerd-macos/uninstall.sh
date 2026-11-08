#!/bin/sh
# SPDX-License-Identifier: MIT
# Stop and remove the service. The GPU returns to the 95 W firmware base.
set -eu
[ "$(id -u)" = 0 ] || { echo 'Use sudo ./uninstall.sh' >&2; exit 1; }
launchctl bootout system/local.nvpowerd-macos 2>/dev/null || true
rm -f /Library/LaunchDaemons/local.nvpowerd-macos.plist /Library/PrivilegedHelperTools/nvpowerd-macos
echo 'Removed. Log: /var/log/nvpowerd-macos.log'
