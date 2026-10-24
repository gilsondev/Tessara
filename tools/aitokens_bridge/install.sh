#!/bin/sh
# Prepara e aplica a instalação do AI Tokens Bridge (launchd + Claude statusLine).
set -e
ROOT="$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)"
BRIDGE="$ROOT/tools/aitokens_bridge"
PY="$ROOT/.venv/bin/python"
PLIST_SRC="$BRIDGE/generated/com.tessera.aitokens-bridge.plist"
PLIST_DST="$HOME/Library/LaunchAgents/com.tessera.aitokens-bridge.plist"
LABEL=com.tessera.aitokens-bridge
LOG_DIR="$HOME/.tessera"
LOG="$LOG_DIR/aitokens-bridge.log"
STATUSLINE="$BRIDGE/statusline_claude.sh"

if [ ! -x "$PY" ]; then
  echo "falta $PY — rode: python3 -m venv .venv && .venv/bin/pip install -r tools/aitokens_bridge/requirements.txt" >&2
  exit 1
fi
"$PY" -c "import serial" 2>/dev/null || "$PY" -m pip install -r "$BRIDGE/requirements.txt"

mkdir -p "$BRIDGE/generated" "$LOG_DIR" "$HOME/Library/LaunchAgents"
chmod +x "$STATUSLINE"

cat > "$PLIST_SRC" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$PY</string>
    <string>$BRIDGE/bridge.py</string>
  </array>
  <key>WorkingDirectory</key>
  <string>$BRIDGE</string>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>ThrottleInterval</key>
  <integer>5</integer>
  <key>StandardOutPath</key>
  <string>$LOG</string>
  <key>StandardErrorPath</key>
  <string>$LOG</string>
  <key>EnvironmentVariables</key>
  <dict>
    <key>PATH</key>
    <string>/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin</string>
    <key>HOME</key>
    <string>$HOME</string>
  </dict>
</dict>
</plist>
EOF

cp "$PLIST_SRC" "$PLIST_DST"

UID_NUM="$(id -u)"
launchctl bootout "gui/$UID_NUM/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$UID_NUM" "$PLIST_DST"
launchctl enable "gui/$UID_NUM/$LABEL" 2>/dev/null || true
launchctl kickstart -k "gui/$UID_NUM/$LABEL" 2>/dev/null || launchctl start "$LABEL" || true

# Claude Code statusLine → ~/.tessera/claude_status.json
"$PY" - <<PY
import json
from pathlib import Path
path = Path.home() / ".claude" / "settings.json"
path.parent.mkdir(parents=True, exist_ok=True)
data = {}
if path.exists():
    try:
        data = json.loads(path.read_text(encoding="utf-8") or "{}")
    except json.JSONDecodeError:
        data = {}
if not isinstance(data, dict):
    data = {}
data["statusLine"] = {"type": "command", "command": r"""$STATUSLINE"""}
path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
print("statusLine →", path)
PY

echo "plist gerado: $PLIST_SRC"
echo "plist ativo:  $PLIST_DST"
echo "log:          $LOG"
launchctl print "gui/$UID_NUM/$LABEL" 2>/dev/null | head -20 || true
