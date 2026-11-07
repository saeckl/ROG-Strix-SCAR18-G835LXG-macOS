#!/bin/bash
# Builds and installs the ASUS hotkey agent for the current user (no sudo).
# Needs ASUSG835WMI.kext for Fn+M4 (performance profiles). After the first start macOS asks for
# Input Monitoring: allow "asus-hotkeys". If you rebuild the binary later, remove it from
# Input Monitoring with "-" and add it again with "+", the permission is tied to the exact file.
set -euo pipefail
cd "$(dirname "$0")"
app="$HOME/Library/Application Support/ASUSHotkeys"
agent="$HOME/Library/LaunchAgents/de.codex.asushotkeys.plist"
mkdir -p "$app" "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
swiftc -O asus-hotkeys.swift -o "$app/asus-hotkeys"
[[ -f "$app/config.json" ]] || cp config.json "$app/config.json"
cat > "$agent" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>de.codex.asushotkeys</string>
  <key>ProgramArguments</key><array><string>$app/asus-hotkeys</string></array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardErrorPath</key><string>$HOME/Library/Logs/asus-hotkeys.log</string>
</dict></plist>
PLIST
launchctl bootout gui/$(id -u)/de.codex.asushotkeys 2>/dev/null || true
launchctl bootstrap gui/$(id -u) "$agent"
sleep 3; tail -3 "$HOME/Library/Logs/asus-hotkeys.log" || true
echo "If the log shows 'IOHIDManagerOpen 0xe00002e2': allow asus-hotkeys under"
echo "System Settings > Privacy & Security > Input Monitoring, then:"
echo "  launchctl kickstart -k gui/$(id -u)/de.codex.asushotkeys"
open "x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent" || true
