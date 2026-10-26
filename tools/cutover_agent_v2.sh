#!/usr/bin/env bash
# Cutover local: legacy Python (com.tessera.agent) → agentd + Tessera.app
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
  # Prefer user-facing binary name (macOS TCC shows this in permission dialogs).
  for cand in Tessera agentd; do
    if [ -x "${target_dir:-$ROOT/agentd/target}/release/$cand" ]; then
      AGENTD_BIN="${target_dir:-$ROOT/agentd/target}/release/$cand"
      break
    fi
  done
  AGENTD_BIN="${AGENTD_BIN:-${target_dir:-$ROOT/agentd/target}/release/Tessera}"
fi
TESSERA_APP="${TESSERA_APP:-$ROOT/apps/agent-ui/src-tauri/target/release/bundle/macos/Tessera.app}"
IPC_PORT="${TESSERA_AGENTD_IPC_PORT:-19792}"
# Default: legacy Python off so only Tessera owns the USB port.
BOOTOUT_LEGACY="${BOOTOUT_LEGACY:-1}"
# Installed process name — never "agentd" in FDA/TCC prompts
INSTALL_BIN_NAME="${INSTALL_BIN_NAME:-Tessera}"

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
# App bundle so Privacy / FDA shows the Tessera icon (not a bare Terminal-like binary).
AGENTD_APP="$INSTALL_ROOT/Tessera.app"
AGENTD_MACOS="$AGENTD_APP/Contents/MacOS"
AGENTD_RES="$AGENTD_APP/Contents/Resources"
LABEL=com.tessera.agentd
LEGACY_LABEL=com.tessera.agent
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/.tessera/agentd.log"
UID_NUM="$(id -u)"
ICNS_SRC="${ICNS_SRC:-$ROOT/apps/agent-ui/src-tauri/icons/icon.icns}"

mkdir -p "$BIN_DIR" "$AGENTD_MACOS" "$AGENTD_RES" "$HOME/Library/LaunchAgents" "$HOME/.tessera"

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

install -m 755 "$AGENTD_BIN" "$AGENTD_MACOS/$INSTALL_BIN_NAME"
# Compat: bin/Tessera → app executable (scripts / old docs)
ln -sfn "../Tessera.app/Contents/MacOS/$INSTALL_BIN_NAME" "$BIN_DIR/$INSTALL_BIN_NAME"
ln -sfn "$INSTALL_BIN_NAME" "$BIN_DIR/agentd"
if [ -f "$ICNS_SRC" ]; then
  cp -f "$ICNS_SRC" "$AGENTD_RES/AppIcon.icns"
else
  echo "cutover: aviso — sem $ICNS_SRC (ícone FDA pode ficar genérico)" >&2
fi
cat > "$AGENTD_APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>pt_BR</string>
  <key>CFBundleExecutable</key><string>Tessera</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundleIdentifier</key><string>com.tessera.agentd</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>Tessera</string>
  <key>CFBundleDisplayName</key><string>Tessera</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1.0</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSLocalNetworkUsageDescription</key>
  <string>O Tessera ajusta o painel na sua rede Wi‑Fi (tessera.local).</string>
  <key>NSBonjourServices</key>
  <array>
    <string>_http._tcp.</string>
  </array>
</dict>
</plist>
PLIST
# Refresh LaunchServices icon cache for this bundle
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$AGENTD_APP" 2>/dev/null || true
migrate_setup_completed

cat > "$PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$AGENTD_MACOS/$INSTALL_BIN_NAME</string>
  </array>
  <key>WorkingDirectory</key><string>$INSTALL_ROOT</string>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>5</integer>
  <key>StandardOutPath</key><string>$LOG</string>
  <key>StandardErrorPath</key><string>$LOG</string>
  <key>EnvironmentVariables</key>
  <dict>
    <key>PATH</key><string>/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/sbin</string>
    <key>HOME</key><string>$HOME</string>
PLIST
# Optional: pin panel portal IP (reqwest falha em *.local / mDNS)
PANEL_IP="$(/sbin/ping -c 1 -t 2 tessera.local 2>/dev/null | sed -n 's/.*(\([0-9.]*\)).*/\1/p' | head -1 || true)"
if [ -n "${PANEL_IP:-}" ]; then
  cat >> "$PLIST" <<PLIST
    <key>TESSERA_PANEL_URL</key><string>http://$PANEL_IP</string>
PLIST
  mkdir -p "$INSTALL_ROOT"
  printf 'http://%s\n' "$PANEL_IP" > "$INSTALL_ROOT/panel_base_url"
fi
cat >> "$PLIST" <<'PLIST'
  </dict>
</dict>
</plist>
PLIST

launchctl bootout "gui/$UID_NUM/$LABEL" 2>/dev/null || true
sleep 1
if ! launchctl bootstrap "gui/$UID_NUM" "$PLIST" 2>/tmp/tessera-bootstrap.err; then
  echo "cutover: bootstrap falhou — tentando de novo…" >&2
  cat /tmp/tessera-bootstrap.err >&2 || true
  sleep 2
  launchctl bootout "gui/$UID_NUM/$LABEL" 2>/dev/null || true
  launchctl bootstrap "gui/$UID_NUM" "$PLIST"
fi
launchctl enable "gui/$UID_NUM/$LABEL" 2>/dev/null || true
# Soft kickstart first; -k if the job never came up
launchctl kickstart "gui/$UID_NUM/$LABEL" 2>/dev/null || true
sleep 1
if ! curl -sf "http://127.0.0.1:${IPC_PORT}/v1/status" >/dev/null 2>&1; then
  launchctl kickstart -k "gui/$UID_NUM/$LABEL" 2>/dev/null || true
fi
echo "cutover: LaunchAgent $LABEL instalado"

if [ -d "$TESSERA_APP" ]; then
  rm -rf "/Applications/Tessera.app"
  cp -R "$TESSERA_APP" /Applications/
  echo "cutover: Tessera.app → /Applications/Tessera.app"
else
  echo "cutover: aviso — Tessera.app não encontrado em $TESSERA_APP" >&2
fi

if [ -d /Applications/Tessera.app ]; then
  open -a Tessera || true
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
