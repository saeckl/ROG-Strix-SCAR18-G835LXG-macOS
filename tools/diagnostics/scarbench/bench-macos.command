#!/bin/bash
# scarbench (CPU/RAM) + sustained GPU load per ASUS profile on macOS; results to LEITSTAND.
clear
B="$(cd "$(dirname "$0")" && pwd)"; R="$(cd "$B/../../.." && pwd)"
WMI="$R/drivers/ASUSG835WMI/build/wmictl"
GPU="$R/nullmoth-fix/bin/sustained"
OUT="/Volumes/LEITSTAND/SCAR-Bench-macOS-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$OUT" || OUT="$HOME/SCAR-Bench-macOS-$(date +%H%M%S)"; mkdir -p "$OUT"
exec > >(tee "$OUT/ablauf.txt") 2>&1
echo "SCAR-Bench macOS: CPU/RAM + GPU je Profil (~12 min). Claude-Fenster minimieren, Maus nicht bewegen. Passwort (fuer powermetrics):"
sudo -v || exit 1
[ -x "$GPU" ] || xcrun swiftc -O "$R/nullmoth-fix/sustained.swift" -o "$GPU"
xcrun clang -O2 -o "$OUT/scarbench" "$B/scarbench.c" -lpthread || exit 1
{ sw_vers; sysctl -n machdep.cpu.brand_string; cat /Library/NullMoth/driver-version; launchctl print system/local.nvpowerd-macos 2>/dev/null | grep -m1 "state"; system_profiler SPMemoryDataType | grep -E "Size|Speed|Part"; } > "$OUT/system.txt"
PREV=$(ioreg -rc ASUSG835WMI -d1 | sed -n 's/.*"Profile" = "\(.*\)"/\1/p')
for P in quiet balanced performance; do
  "$WMI" profile=$P >/dev/null; sleep 10; echo "===== Profil $P"
  "$OUT/scarbench" | tee "$OUT/cpu-$P.txt"
  sudo powermetrics --samplers cpu_power -i 5000 -n 12 > "$OUT/powermetrics-$P.txt" 2>&1 &
  "$OUT/scarbench" sustain 60 | tee "$OUT/sustain-$P.txt"; wait
  grep -iE "average frequency as fraction|package power" "$OUT/powermetrics-$P.txt" | tail -6
  echo "-- GPU-Dauerlast (sustained.swift)"; "$GPU" 2>/dev/null | grep -E "^s" | tail -5 | tee "$OUT/gpu-$P.txt"
done
"$WMI" profile=$PREV >/dev/null
sudo chown -R $(id -u) "$OUT"
echo "Fertig: $OUT"; afplay /System/Library/Sounds/Glass.aiff
read -r -p "Enter schliesst." _ </dev/tty
