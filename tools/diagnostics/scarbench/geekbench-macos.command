#!/bin/bash
# Geekbench 7: CPU + GPU (Metal) on macOS, GPU with and without nvpowerd-macos, profile Turbo.
clear
B="$(cd "$(dirname "$0")" && pwd)"; R="$(cd "$B/../../.." && pwd)"; WMI="$R/drivers/ASUSG835WMI/build/wmictl"
G="/Applications/Geekbench 7.app/Contents/Resources/geekbench7"
OUT="/Volumes/LEITSTAND/SCAR-Geekbench-macOS-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$OUT" || exit 1
exec > >(tee "$OUT/ablauf.txt") 2>&1
echo "Geekbench 7 macOS (~15 min, Ergebnisse werden oeffentlich hochgeladen). Claude-Fenster minimieren. Passwort:"
sudo -v || exit 1
( while true; do sudo -n true; sleep 50; done ) & KEEP=$!
PREV=$(ioreg -rc ASUSG835WMI -d1 | sed -n 's/.*"Profile" = "\(.*\)"/\1/p')
"$WMI" profile=performance >/dev/null; sleep 15
echo "== CPU (Turbo)"; "$G" --cpu > "$OUT/cpu.txt" 2>&1; grep -iE "score|browser.geekbench|https" "$OUT/cpu.txt" | tail -6
sleep 30
echo "== GPU Metal, Dienst AN (Turbo)"; "$G" --gpu Metal > "$OUT/gpu-metal-dienst-an.txt" 2>&1; grep -iE "score|https" "$OUT/gpu-metal-dienst-an.txt" | tail -4
grep "accepted" /var/log/nvpowerd-macos.log | tail -3
sudo launchctl bootout system/local.nvpowerd-macos; sleep 30
echo "== GPU Metal, Dienst AUS (Turbo, 95 W)"; "$G" --gpu Metal > "$OUT/gpu-metal-dienst-aus.txt" 2>&1; grep -iE "score|https" "$OUT/gpu-metal-dienst-aus.txt" | tail -4
sudo launchctl bootstrap system /Library/LaunchDaemons/local.nvpowerd-macos.plist
"$WMI" profile=$PREV >/dev/null; kill $KEEP 2>/dev/null
echo "Fertig: $OUT"; afplay /System/Library/Sounds/Glass.aiff
read -r -p "Enter schliesst." _ </dev/tty
