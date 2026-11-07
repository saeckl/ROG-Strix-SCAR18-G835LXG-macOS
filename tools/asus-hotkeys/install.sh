#!/bin/bash
# Builds and installs the ASUS hotkey agent and the menu bar app "ROG Licht" for the current
# user (no sudo). Needs ASUSG835WMI.kext for the performance profiles.
# macOS asks for Input Monitoring for asus-hotkeys. The permission is tied to the exact binary:
# after every rebuild remove it in System Settings with "-" and add it again with "+".
set -euo pipefail
cd "$(dirname "$0")"
app="$HOME/Library/Application Support/ASUSHotkeys"
la="$HOME/Library/LaunchAgents"
mkdir -p "$app" "$la" "$HOME/Library/Logs" build
cp asus-hotkeys.swift build/main.swift
swiftc -O build/main.swift Lighting.swift -o "$app/asus-hotkeys"
swiftc -O -parse-as-library ROGLicht.swift Lighting.swift -o build/ROGLicht
[[ -f "$app/config.json" ]] || cp config.json "$app/config.json"

bundle="/Applications/ROG Licht.app"
rm -rf "$bundle"; mkdir -p "$bundle/Contents/MacOS"; cp build/ROGLicht "$bundle/Contents/MacOS/ROGLicht"
cat > "$bundle/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>ROGLicht</string>
  <key>CFBundleIdentifier</key><string>de.codex.roglicht</string>
  <key>CFBundleName</key><string>ROG Licht</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>LSUIElement</key><true/>
</dict></plist>
PLIST
codesign -s - --force "$bundle"

agent() {  # label, program
cat > "$la/$1.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$1</string>
  <key>ProgramArguments</key><array><string>$2</string></array>
  <key>RunAtLoad</key><true/>
  $3
</dict></plist>
PLIST
launchctl bootout gui/$(id -u)/$1 2>/dev/null || true
launchctl bootstrap gui/$(id -u) "$la/$1.plist"
}
agent de.codex.asushotkeys "$app/asus-hotkeys" "<key>KeepAlive</key><true/><key>StandardErrorPath</key><string>$HOME/Library/Logs/asus-hotkeys.log</string>"
agent de.codex.roglicht "$bundle/Contents/MacOS/ROGLicht" ""
sleep 3; tail -3 "$HOME/Library/Logs/asus-hotkeys.log" || true
echo "If the log shows 'IOHIDManagerOpen 0xe00002e2': allow asus-hotkeys under"
echo "System Settings > Privacy & Security > Input Monitoring, then:"
echo "  launchctl kickstart -k gui/$(id -u)/de.codex.asushotkeys"
open "x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent" || true
