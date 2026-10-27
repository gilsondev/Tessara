#!/bin/bash
# Tessera Agent — install from GitHub Pages (duplo clique ou: bash install.command)
# Uso: curl -fsSL https://gilsondev.github.io/Tessera/install.command | bash
set -euo pipefail

PAGES="${1:-https://gilsondev.github.io/Tessera}"
MANIFEST="${PAGES%/}/latest.json"
INSTALL_ROOT="$HOME/.tessera/agentd"
BIN_DIR="$INSTALL_ROOT/bin"
INSTALL_BIN_NAME=Tessera
LABEL=com.tessera.agentd
LEGACY_LABEL=com.tessera.agent
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/.tessera/agentd.log"

mkdir -p "$BIN_DIR" "$HOME/.tessera" "$HOME/Library/LaunchAgents"

echo "Tessera: lendo $MANIFEST …"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
curl -fsSL "$MANIFEST" -o "$TMP/latest.json"

python3 - "$TMP/latest.json" "$TMP" <<'PY'
import json, sys, urllib.request
from pathlib import Path
doc = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
out = Path(sys.argv[2])
agentd = doc.get("agentd_url") or ""
app = doc.get("app_url") or ""
ver = doc.get("version") or "?"
if not agentd:
    sys.exit("latest.json sem agentd_url — rode o workflow agent-release primeiro")
(out / "version").write_text(ver + "\n", encoding="utf-8")
(out / "agentd_url").write_text(agentd, encoding="utf-8")
(out / "app_url").write_text(app, encoding="utf-8")
sha = (doc.get("sha256") or {}).get("agentd") or ""
(out / "agentd_sha").write_text(sha, encoding="utf-8")
print(f"versão {ver}")
PY

VER="$(cat "$TMP/version")"
AGENTD_URL="$(cat "$TMP/agentd_url")"
APP_URL="$(cat "$TMP/app_url")"
EXPECT_SHA="$(cat "$TMP/agentd_sha")"

echo "Tessera: baixando agentd.tgz ($VER) …"
curl -fsSL "$AGENTD_URL" -o "$TMP/agentd.tgz"
if [ -n "$EXPECT_SHA" ]; then
  GOT="$(shasum -a 256 "$TMP/agentd.tgz" | awk '{print $1}')"
  if [ "$GOT" != "$EXPECT_SHA" ]; then
    echo "install: SHA-256 não confere (esperado $EXPECT_SHA, obtido $GOT)" >&2
    exit 1
  fi
fi

rm -rf "$INSTALL_ROOT/staging"
mkdir -p "$INSTALL_ROOT/staging"
tar -C "$INSTALL_ROOT/staging" -xzf "$TMP/agentd.tgz"
if [ -x "$INSTALL_ROOT/staging/bin/Tessera" ]; then
  install -m 755 "$INSTALL_ROOT/staging/bin/Tessera" "$BIN_DIR/$INSTALL_BIN_NAME"
elif [ -x "$INSTALL_ROOT/staging/bin/agentd" ]; then
  install -m 755 "$INSTALL_ROOT/staging/bin/agentd" "$BIN_DIR/$INSTALL_BIN_NAME"
else
  echo "install: falta bin/Tessera no pacote" >&2
  exit 1
fi
ln -sfn "$INSTALL_BIN_NAME" "$BIN_DIR/agentd"

if [ -d "$INSTALL_ROOT/staging/Tessera.app" ]; then
  rm -rf "/Applications/Tessera.app"
  cp -R "$INSTALL_ROOT/staging/Tessera.app" /Applications/
elif [ -n "$APP_URL" ] && [[ "$APP_URL" == *"/releases/"* ]]; then
  echo "Tessera: baixando Tessera.app.zip …"
  curl -fsSL "$APP_URL" -o "$TMP/Tessera.app.zip"
  rm -rf "$TMP/appout"
  mkdir -p "$TMP/appout"
  ditto -x -k "$TMP/Tessera.app.zip" "$TMP/appout"
  APP_SRC="$(find "$TMP/appout" -maxdepth 2 -name 'Tessera.app' -type d | head -1)"
  if [ -n "$APP_SRC" ]; then
    rm -rf "/Applications/Tessera.app"
    cp -R "$APP_SRC" /Applications/
  fi
fi
rm -rf "$INSTALL_ROOT/staging"

AGENTD="$BIN_DIR/$INSTALL_BIN_NAME"
APP="$INSTALL_ROOT/Tessera Agent.app"
mkdir -p "$APP/Contents/MacOS"
cp "$AGENTD" "$APP/Contents/MacOS/Tessera"
chmod 755 "$APP/Contents/MacOS/Tessera"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>Tessera</string>
  <key>CFBundleIdentifier</key><string>dev.tessera.agentd</string>
  <key>CFBundleName</key><string>Tessera Agent</string>
  <key>CFBundleDisplayName</key><string>Tessera Agent</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1</string>
  <key>LSUIElement</key><true/>
</dict></plist>
PLIST
codesign --force --sign - "$APP" >/dev/null 2>&1 || true
AGENTD="$APP/Contents/MacOS/Tessera"
UID_NUM="$(id -u)"

# Desliga o LaunchAgent Python legado, se ainda existir.
launchctl bootout "gui/$UID_NUM/$LEGACY_LABEL" 2>/dev/null || true
rm -f "$HOME/Library/LaunchAgents/$LEGACY_LABEL.plist"

cat > "$PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$AGENTD</string>
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

echo "Tessera Agent $VER instalado → $INSTALL_ROOT"
echo "log: $LOG"
echo "Site: $PAGES"
if [ -d /Applications/Tessera.app ]; then
  # curl não marca quarentena. Se a cópia herdou a marca de um zip do navegador, tira.
  xattr -cr /Applications/Tessera.app 2>/dev/null || true
  open -a Tessera || true
fi
