#!/usr/bin/env bash
# Cutover local: legacy Python (com.tessera.agent) → agentd + Tessara.app
#
# Pré-req: mise run agentd-build && mise run agent-ui-build (ou binários via env)
#
# Uso:
#   tools/cutover_agent_v2.sh [--bootout-legacy]
#   BOOTOUT_LEGACY=1 tools/cutover_agent_v2.sh
#
# --bootout-legacy  para e remove com.tessera.agent (nunca os dois LaunchAgents no cutover final)
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
if [ -z "${AGENTD_BIN:-}" ]; then
  target_dir="$(cd "$ROOT/agentd" && cargo metadata --format-version=1 --no-deps 2>/dev/null \
    | python3 -c "import json,sys; print(json.load(sys.stdin)['target_directory'])")"
  AGENTD_BIN="${target_dir:-$ROOT/agentd/target}/release/agentd"
fi
TESSARA_APP="${TESSARA_APP:-$ROOT/apps/agent-ui/src-tauri/target/release/bundle/macos/Tessara.app}"
IPC_PORT="${TESSARA_AGENTD_IPC_PORT:-19792}"
BOOTOUT_LEGACY="${BOOTOUT_LEGACY:-0}"

for arg in "$@"; do
  case "$arg" in
    --bootout-legacy) BOOTOUT_LEGACY=1 ;;
    -h|--help)
      sed -n '1,12p' "$0"
      exit 0
      ;;
    *)
      echo "cutover: argumento desconhecido: $arg" >&2
      exit 1
      ;;
  esac
done

if [ ! -x "$AGENTD_BIN" ]; then
  echo "cutover: falta $AGENTD_BIN — rode: mise run agentd-build" >&2
  exit 1
fi

INSTALL_ROOT="$HOME/.tessera/agentd"
BIN_DIR="$INSTALL_ROOT/bin"
LABEL=com.tessera.agentd
LEGACY_LABEL=com.tessera.agent
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/.tessera/agentd.log"
UID_NUM="$(id -u)"

mkdir -p "$BIN_DIR" "$HOME/Library/LaunchAgents" "$HOME/.tessera"

legacy_had_config() {
  [ -x "$HOME/.tessera/agent/.venv/bin/python" ] \
    || [ -f "$HOME/Library/LaunchAgents/com.tessera.agent.plist" ] \
    || [ -d "$HOME/.tessera/agent/src/agent" ]
}

migrate_setup_completed() {
  if ! legacy_had_config; then
    return 0
  fi
  local cfg="$HOME/.tessera/agentd/config.toml"
  mkdir -p "$HOME/.tessera/agentd"
  if [ ! -f "$cfg" ]; then
    cat > "$cfg" <<'TOML'
media = true
focus = true
aitokens = true
calendar = true
setup_completed = true
TOML
    echo "cutover: setup_completed=true (legacy já configurado)"
    return 0
  fi
  python3 - "$cfg" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
lines = path.read_text(encoding="utf-8").splitlines()
out, found = [], False
for line in lines:
    if line.strip().startswith("setup_completed"):
        out.append("setup_completed = true")
        found = True
    else:
        out.append(line)
if not found:
    out.append("setup_completed = true")
path.write_text("\n".join(out) + "\n", encoding="utf-8")
PY
  echo "cutover: setup_completed=true (legacy já configurado)"
}

if [ "$BOOTOUT_LEGACY" = 1 ]; then
  echo "cutover: desligando legacy $LEGACY_LABEL …"
  launchctl bootout "gui/$UID_NUM/$LEGACY_LABEL" 2>/dev/null || true
  rm -f "$HOME/Library/LaunchAgents/$LEGACY_LABEL.plist"
  rm -f "$HOME/Library/LaunchAgents/com.tessera.aitokens-bridge.plist"
else
  echo "cutover: legacy não alterado (use --bootout-legacy no cutover final)"
fi

install -m 755 "$AGENTD_BIN" "$BIN_DIR/agentd"
migrate_setup_completed

cat > "$PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$BIN_DIR/agentd</string>
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
  </dict>
</dict>
</plist>
PLIST

launchctl bootout "gui/$UID_NUM/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$UID_NUM" "$PLIST"
launchctl enable "gui/$UID_NUM/$LABEL" 2>/dev/null || true
launchctl kickstart -k "gui/$UID_NUM/$LABEL" 2>/dev/null || true
echo "cutover: LaunchAgent $LABEL instalado"

if [ -d "$TESSARA_APP" ]; then
  rm -rf "/Applications/Tessara.app"
  cp -R "$TESSARA_APP" /Applications/
  echo "cutover: Tessara.app → /Applications/Tessara.app"
else
  echo "cutover: aviso — Tessara.app não encontrado em $TESSARA_APP" >&2
fi

if [ -d /Applications/Tessara.app ]; then
  open -a Tessara || true
fi

echo "cutover: aguardando Doctor (127.0.0.1:$IPC_PORT) …"
deadline=$((SECONDS + 90))
while [ "$SECONDS" -lt "$deadline" ]; do
  if body="$(curl -sf "http://127.0.0.1:${IPC_PORT}/v1/doctor" 2>/dev/null)"; then
    if python3 -c "import json,sys; d=json.loads(sys.argv[1]); sys.exit(0 if all(c.get('level')!='bad' for c in d.get('checks',[])) else 1)" "$body"; then
      summary="$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['summary'])" "$body")"
      echo "cutover: Doctor ok — $summary"
      echo "cutover: concluído. Rode o gate: tools/agent_v2_gate.md"
      exit 0
    fi
  fi
  sleep 2
done

echo "cutover: Doctor não ficou verde a tempo — veja: curl -s http://127.0.0.1:${IPC_PORT}/v1/doctor | python3 -m json.tool" >&2
exit 1
