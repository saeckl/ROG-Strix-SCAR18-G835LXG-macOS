#!/bin/bash
# Gegenstueck zu SCAR-Messung-Windows.ps1: Profile per ASUS-Kext umschalten, GPU-Dauerlast und WoW-FPS messen.
clear
R="$(cd "$(dirname "$0")/.." && pwd)"; B="$R/nullmoth-fix/bin"; WMI="$R/drivers/ASUSG835WMI/build/wmictl"  # build sustained.swift into nullmoth-fix/bin first
OUT="/Volumes/LEITSTAND/SCAR-Messung-macOS-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$OUT"
LOG=/tmp/nvmtl.log
say() { echo "$(date +%H:%M:%S) $*" | tee -a "$OUT/ablauf.txt"; }
setp() { "$WMI" "profile=$1" >/dev/null; sleep 1; say "  Profil -> $1 ($("$WMI" 'profile?' >/dev/null; ioreg -r -c ASUSG835WMI -l -w0 | grep -oE '"Profile" = "[a-z]+"'))"; }
declare -a NAMES=(Leise Leistung Turbo); declare -a IDS=(quiet balanced performance)
launchctl setenv NVMTL_PROCESS_ENV 1
say "=== SCAR-Messung macOS (Treiber $(cat /Library/NullMoth/driver-version)) ==="
say "Teil 1: Desktop - GPU-Dauerlast je Profil (Maus nicht bewegen)"
osascript -e 'quit app "World of Warcraft"' -e 'quit app "Battle.net"' 2>/dev/null; sleep 3
for i in 0 1 2; do
  setp "${IDS[$i]}"; sleep 8
  "$B/sustained" 2>/dev/null | grep -E "^s" > "$OUT/gpu-${NAMES[$i]}.txt"
  say "  ${NAMES[$i]}: $(tail -5 "$OUT/gpu-${NAMES[$i]}.txt" | sed -E 's/.*Mittel ([0-9.]+) TFLOPS.*bestes ([0-9.]+).*/\1 \2/' | awk '{sum+=$1; b+=$2} END {printf "Dauerlast %.1f TFLOPS (~%.0f MHz), bestes %.1f", sum/NR, sum/NR*1e12/(82*128*2)/1e6, b/NR}')"
done
say "Teil 2: Starte jetzt Battle.net und WoW, geh an die Vergleichsstelle und bleib stehen."
afplay /System/Library/Sounds/Glass.aiff
until pgrep -f "World of Warcraft.app/Contents/MacOS/World of Warcraft" >/dev/null; do sleep 2; done
say "WoW erkannt, warte 60 s Ladezeit"; sleep 60
for i in 0 1 2; do
  setp "${IDS[$i]}"; sleep 8
  start=$(stat -f %z "$LOG" 2>/dev/null || echo 0); sleep 15
  fps=$(tail -c +$((start+1)) "$LOG" 2>/dev/null | grep -oE 'fps: [0-9.]+' | awk '{s+=$2; n++} END {if (n) printf "%.1f", s/n; else print "?"}')
  say "  ${NAMES[$i]}: FPS $fps"
done
setp quiet
launchctl unsetenv NVMTL_PROCESS_ENV
afplay /System/Library/Sounds/Glass.aiff
say "Fertig. Ergebnisse in $OUT"
read -r -p "Enter schliesst." _
