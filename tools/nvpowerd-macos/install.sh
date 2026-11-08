#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
[ "$(uname -s)" = Darwin ] || { echo 'Run this on macOS only.' >&2; exit 1; }
[ "$(id -u)" = 0 ] || { echo 'Use sudo ./install.sh RM_VERSION 150|175' >&2; exit 1; }
[ "$#" = 2 ] || { echo 'Usage: sudo ./install.sh RM_VERSION 150|175' >&2; exit 1; }
case "$1" in ''|*[!0-9.]*) echo 'RM_VERSION must contain digits and dots only.' >&2; exit 1;; esac
case "$2" in 150|175) ;; *) echo 'Only 150 or 175 W.' >&2; exit 1;; esac
cd -- "$(dirname -- "$0")"
[ -x build/nvpowerd-macos ] || { echo 'Build with make on macOS first.' >&2; exit 1; }
file build/nvpowerd-macos | grep -q 'Mach-O.*x86_64' || { echo 'Expected an Intel Mach-O executable.' >&2; exit 1; }
if launchctl print system/local.nvpowerd-macos >/dev/null 2>&1; then
  echo 'Stop/unload the existing service before replacing it (see README.txt).' >&2; exit 1
fi
umask 077
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT HUP INT TERM
sed -e "s/@RM_VERSION@/$1/g" -e "s/@WATTS@/$2/g" launchd/local.nvpowerd-macos.plist.in > "$tmp"
plutil -lint "$tmp"
install -d -o root -g wheel -m 0755 /Library/PrivilegedHelperTools
install -o root -g wheel -m 0755 build/nvpowerd-macos /Library/PrivilegedHelperTools/nvpowerd-macos
install -o root -g wheel -m 0644 "$tmp" /Library/LaunchDaemons/local.nvpowerd-macos.plist
printf '%s\n' 'Installed. Start after the foreground test succeeds:' \
  'sudo launchctl bootstrap system /Library/LaunchDaemons/local.nvpowerd-macos.plist'
