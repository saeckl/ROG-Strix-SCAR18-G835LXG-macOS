#!/bin/bash
# Turns one NullMoth Metal option (NVMTL_*) on or off in the user block of nvrm610.conf.
# The block between "# --- my settings, kept on update ---" and "# --- end my settings ---" survives
# driver updates. A backup of the file is kept next to it. Restart the app afterwards.
#   sudo ./nvmtl-option.sh NVMTL_NAK_NO_PREDLOAD on
#   sudo ./nvmtl-option.sh NVMTL_NAK_NO_PREDLOAD off
set -euo pipefail
C=/Library/GPUBundles/nvmtl/nvrm610.conf
START="# --- my settings, kept on update ---"; END="# --- end my settings ---"
[[ $(id -u) == 0 ]] || { echo "run with sudo"; exit 1; }
[[ $# == 2 && $1 =~ ^NVMTL_[A-Z0-9_]+$ && $2 =~ ^(on|off)$ ]] || { echo "usage: sudo $0 NVMTL_OPTION on|off"; exit 1; }
OPT=$1; MODE=$2
[[ -f $C ]] || { echo "$C not found (NullMoth driver installed?)"; exit 1; }
grep -qxF "$END" "$C" || printf '%s\n%s\n' "$START" "$END" >> "$C"
cp -p "$C" "$C.backup-$(date +%Y%m%d-%H%M%S)"
tmp=$(mktemp)
awk -v opt="$OPT" -v mode="$MODE" -v end="$END" '
  $0 ~ "^"opt"=" {next}
  $0 == end && mode == "on" {print opt"=1"}
  {print}' "$C" > "$tmp"
install -o root -g wheel -m 0644 "$tmp" "$C"; rm -f "$tmp"
sed -n "/^$START\$/,/^$END\$/p" "$C"
