#!/usr/bin/env bash
# Empacota Agent v2 (agentd + Tessera.app) para o cartão SD: /tessera/agentd/
# Uso: ./pack_sd.sh [destino]
#   destino = raiz do volume SD montado, ou pasta staging (default: ./dist/sd)
#
# SD nunca leva segredos (OAuth, tokens, .env). O script falha se achar paths proibidos.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
REPO="$(CDPATH= cd -- "$ROOT/.." && pwd)"
DEST="${1:-$ROOT/dist/sd}"
OUT="$DEST/tessera/agentd"
STAGE="$ROOT/dist/stage-agentd"
PKG="$STAGE/pkg"

VERSION="$(sed -n 's/^version = "\(.*\)"/\1/p' "$ROOT/Cargo.toml" | head -1)"
if [ -z "$VERSION" ]; then
  echo "pack_sd: não achei version em agentd/Cargo.toml" >&2
  exit 1
fi

FORBIDDEN_NAMES='client.json secrets.h .env token.json refresh_token'

assert_no_secrets() {
  local dir="$1"
  local found=""
  local name
  for name in $FORBIDDEN_NAMES; do
    if find "$dir" -name "$name" -print -quit 2>/dev/null | grep -q .; then
      found="${found:+$found, }$name"
    fi
  done
  if [ -n "$found" ]; then
    echo "pack_sd: recusado — artefato de segredo no stage ($found)" >&2
    exit 1
  fi
  if find "$dir" -path '*/.tessera/calendar/*' -print -quit 2>/dev/null | grep -q .; then
    echo "pack_sd: recusado — calendar OAuth não pode ir no SD" >&2
    exit 1
  fi
}

resolve_agentd_bin() {
  if [ -n "${AGENTD_BIN:-}" ]; then
    printf '%s' "$AGENTD_BIN"
    return
  fi
  local target_dir cand
  target_dir="$(cd "$ROOT" && cargo metadata --format-version=1 --no-deps 2>/dev/null \
    | python3 -c "import json,sys; print(json.load(sys.stdin)['target_directory'])")"
  if [ -z "$target_dir" ]; then
    target_dir="$ROOT/target"
  fi
  # Prefer Tessera (TCC-friendly); fall back to legacy agentd binary name.
  for cand in Tessera agentd; do
    if [ -x "$target_dir/release/$cand" ]; then
      printf '%s/release/%s' "$target_dir" "$cand"
      return
    fi
  done
  printf '%s/release/Tessera' "$target_dir"
}

TESSERA_APP="${TESSERA_APP:-$REPO/apps/agent-ui/src-tauri/target/release/bundle/macos/Tessera.app}"

if [ "${SKIP_BUILD:-0}" != 1 ]; then
  echo "pack_sd: cargo build --release …"
  (cd "$ROOT" && cargo build --release -p agentd)
fi

AGENTD_BIN="$(resolve_agentd_bin)"

if [ ! -x "$AGENTD_BIN" ]; then
  echo "pack_sd: binário ausente: $AGENTD_BIN (ou defina AGENTD_BIN / SKIP_BUILD=1)" >&2
  exit 1
fi

if [ "${SKIP_APP:-0}" != 1 ]; then
  if [ ! -d "$TESSERA_APP" ]; then
    echo "pack_sd: Tessera.app ausente: $TESSERA_APP" >&2
    echo "  rode: mise run agent-ui-build   ou   SKIP_APP=1 (só agentd)" >&2
    exit 1
  fi
fi

rm -rf "$STAGE"
mkdir -p "$PKG/bin" "$OUT"

cp "$AGENTD_BIN" "$PKG/bin/Tessera"
chmod +x "$PKG/bin/Tessera"
ln -sfn Tessera "$PKG/bin/agentd"
printf '%s\n' "$VERSION" > "$PKG/VERSION"

if [ "${SKIP_APP:-0}" != 1 ]; then
  bash "$REPO/tools/bundle_agentd_into_app.sh" "$TESSERA_APP" "$AGENTD_BIN"
  bash "$REPO/tools/sign_macos_app.sh" "$TESSERA_APP"
  cp -R "$TESSERA_APP" "$PKG/Tessera.app"
fi

assert_no_secrets "$PKG"

tar -C "$PKG" -czf "$OUT/agentd.tgz" .
printf '%s\n' "$VERSION" > "$OUT/agentd.version"
# Flat copies at /tessera/ — same layout the portal PUT writes (FAT, no subfolder).
mkdir -p "$DEST/tessera"
cp "$OUT/agentd.tgz" "$DEST/tessera/agentd.tgz"
cp "$OUT/agentd.version" "$DEST/tessera/agentd.version"

cat > "$OUT/install.command" <<'EOF'
#!/bin/bash
# Tessera Agent v2 — bootstrap no Mac (duplo clique ou: bash install.command [http://painel])
set -euo pipefail
PANEL="${1:-http://tessera.local}"
HERE="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
INSTALL_ROOT="$HOME/.tessera/agentd"
BIN_DIR="$INSTALL_ROOT/bin"
INSTALL_BIN_NAME=Tessera
LABEL=com.tessera.agentd
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG_DIR="$HOME/.tessera"
LOG="$LOG_DIR/agentd.log"
mkdir -p "$BIN_DIR" "$LOG_DIR" "$HOME/Library/LaunchAgents"

if [ -f "$HERE/agentd.tgz" ]; then
  TGZ="$HERE/agentd.tgz"
else
  echo "baixando agentd.tgz de $PANEL …"
  curl -fsSL "$PANEL/agentd/agentd.tgz" -o /tmp/tessera-agentd.tgz
  TGZ=/tmp/tessera-agentd.tgz
fi

rm -rf "$INSTALL_ROOT/staging"
mkdir -p "$INSTALL_ROOT/staging"
tar -C "$INSTALL_ROOT/staging" -xzf "$TGZ"
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
fi
rm -rf "$INSTALL_ROOT/staging"

AGENTD="$BIN_DIR/$INSTALL_BIN_NAME"
UID_NUM="$(id -u)"

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
echo "Tessera agentd instalado → $INSTALL_ROOT"
echo "log: $LOG"
if [ -d /Applications/Tessera.app ]; then
  open -a Tessera || true
fi
EOF
chmod +x "$OUT/install.command"

echo "SD pack v2 em $OUT (version $VERSION)"
ls -la "$OUT"
