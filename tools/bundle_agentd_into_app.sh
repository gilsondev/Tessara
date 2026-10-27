#!/bin/bash
# Put the USB agent inside Tessera.app before sealing the bundle.
# The downloaded app is only the window until first open copies this file
# into ~/.tessera and registers the LaunchAgent.
set -euo pipefail

APP="${1:-}"
BIN="${2:-}"
if [ -z "$APP" ] || [ ! -d "$APP" ]; then
  echo "bundle: uso: tools/bundle_agentd_into_app.sh Tessera.app /caminho/do/agente" >&2
  exit 1
fi
if [ ! -x "$BIN" ]; then
  echo "bundle: binário ausente: $BIN" >&2
  exit 1
fi

mkdir -p "$APP/Contents/Resources"
install -m 755 "$BIN" "$APP/Contents/Resources/tessera-agentd"
echo "bundle: agente dentro do app — $APP"
