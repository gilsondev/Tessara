#!/bin/sh
# Empacota o Tessara Agent para o cartão SD do painel: /tessera/agent/
# Uso: ./pack_sd.sh [destino]
#   destino = raiz do volume SD montado, ou pasta staging (default: ./dist/sd)
set -e
ROOT="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
DEST="${1:-$ROOT/dist/sd}"
OUT="$DEST/tessera/agent"
STAGE="$ROOT/dist/stage"

rm -rf "$STAGE"
mkdir -p "$STAGE/pkg/src" "$STAGE/pkg/scripts" "$OUT"
cp -R "$ROOT/src/agent" "$STAGE/pkg/src/"
cp "$ROOT/requirements.txt" "$STAGE/pkg/"
cp "$ROOT/scripts/statusline_claude.sh" "$STAGE/pkg/scripts/"
chmod +x "$STAGE/pkg/scripts/statusline_claude.sh"

tar -C "$STAGE/pkg" -czf "$OUT/agent.tgz" .

cat > "$OUT/install.command" <<'EOF'
#!/bin/bash
# Tessara Agent — bootstrap no Mac (duplo clique ou: bash install.command [http://painel])
set -euo pipefail
PANEL="${1:-http://tessara.local}"
HERE="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
INSTALL_ROOT="$HOME/.tessera/agent"
LABEL=com.tessera.agent
OLD_LABEL=com.tessera.aitokens-bridge
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG_DIR="$HOME/.tessera"
LOG="$LOG_DIR/agent.log"
mkdir -p "$INSTALL_ROOT" "$LOG_DIR" "$HOME/Library/LaunchAgents"

if [ -f "$HERE/agent.tgz" ]; then
  TGZ="$HERE/agent.tgz"
else
  echo "baixando agent.tgz de $PANEL …"
  curl -fsSL "$PANEL/agent/agent.tgz" -o /tmp/tessera-agent.tgz
  TGZ=/tmp/tessera-agent.tgz
fi

# Atualiza código sem apagar o .venv (pip reaplicado abaixo).
find "$INSTALL_ROOT" -mindepth 1 -maxdepth 1 ! -name '.venv' -exec rm -rf {} +
tar -C "$INSTALL_ROOT" -xzf "$TGZ"
chmod +x "$INSTALL_ROOT/scripts/statusline_claude.sh" 2>/dev/null || true

PY="$INSTALL_ROOT/.venv/bin/python"
if [ ! -x "$PY" ]; then
  python3 -m venv "$INSTALL_ROOT/.venv"
fi
echo "deps…"
"$INSTALL_ROOT/.venv/bin/pip" install -q -r "$INSTALL_ROOT/requirements.txt"

STATUSLINE="$INSTALL_ROOT/scripts/statusline_claude.sh"
CLAUDE_SETTINGS="$HOME/.claude/settings.json"
if [ -x "$STATUSLINE" ]; then
  mkdir -p "$HOME/.claude"
  if [ ! -f "$CLAUDE_SETTINGS" ]; then
    printf '{\n  "statusLine": { "type": "command", "command": "%s" }\n}\n' "$STATUSLINE" > "$CLAUDE_SETTINGS"
  else
    python3 - "$CLAUDE_SETTINGS" "$STATUSLINE" <<'PY'
import json, sys
from pathlib import Path
path, cmd = Path(sys.argv[1]), sys.argv[2]
data = json.loads(path.read_text(encoding="utf-8")) if path.is_file() else {}
data["statusLine"] = {"type": "command", "command": cmd}
path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
PY
  fi
fi

UID_NUM="$(id -u)"
launchctl bootout "gui/$UID_NUM/$OLD_LABEL" 2>/dev/null || true
launchctl bootout "gui/$UID_NUM/$LABEL" 2>/dev/null || true
rm -f "$HOME/Library/LaunchAgents/$OLD_LABEL.plist"

cat > "$PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$PY</string>
    <string>-m</string>
    <string>agent</string>
  </array>
  <key>WorkingDirectory</key><string>$INSTALL_ROOT</string>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>5</integer>
  <key>StandardOutPath</key><string>$LOG</string>
  <key>StandardErrorPath</key><string>$LOG</string>
  <key>EnvironmentVariables</key>
  <dict>
    <key>PATH</key><string>/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin</string>
    <key>HOME</key><string>$HOME</string>
    <key>PYTHONPATH</key><string>$INSTALL_ROOT/src</string>
  </dict>
</dict>
</plist>
PLIST

launchctl bootstrap "gui/$UID_NUM" "$PLIST"
launchctl enable "gui/$UID_NUM/$LABEL" 2>/dev/null || true
launchctl kickstart -k "gui/$UID_NUM/$LABEL" 2>/dev/null || true
echo "Tessara Agent instalado → $INSTALL_ROOT"
echo "log: $LOG"
EOF
chmod +x "$OUT/install.command"

echo "SD pack em $OUT"
ls -la "$OUT"
