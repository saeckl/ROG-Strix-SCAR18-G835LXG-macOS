#!/bin/bash
# Build and install clamshell-agent as a per-user LaunchAgent (no admin rights needed).
#   ./install.sh            build + install + start
#   ./install.sh uninstall  stop + remove (built-in panel is switched back on)
set -eu
cd "$(dirname "$0")"
LABEL=as.saeckl.clamshell
DIR="$HOME/Library/Application Support/SCAR-Clamshell"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/clamshell-agent.log"

if [ "${1:-}" = uninstall ]; then
  launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
  rm -f "$PLIST" "$DIR/clamshell-agent"
  echo "removed"; exit 0
fi

mkdir -p "$DIR" "$HOME/Library/LaunchAgents"
xcrun swiftc -O clamshell.swift -o "$DIR/clamshell-agent"
cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key><array><string>$DIR/clamshell-agent</string></array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ProcessType</key><string>Interactive</string>
  <key>StandardOutPath</key><string>$LOG</string>
  <key>StandardErrorPath</key><string>$LOG</string>
</dict></plist>
EOF
plutil -lint "$PLIST" >/dev/null
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"
sleep 1
tail -2 "$LOG"
echo "installed: $PLIST (log: $LOG)"
