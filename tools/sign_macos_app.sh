#!/bin/bash
# Seal a Tessera.app as one bundle.
#
# `tauri build` leaves a linker signature on the Mach-O only. Info.plist and
# the icon are not part of it, so a browser download fails Gatekeeper with
# "code has no resources but signature indicates they must be present" and
# macOS reports the app as damaged. Signing the bundle ad-hoc binds those
# files. This is not notarization: the first open from the internet still
# needs "Open Anyway".
set -euo pipefail

APP="${1:-}"
if [ -z "$APP" ] || [ ! -d "$APP" ]; then
  echo "sign: uso: tools/sign_macos_app.sh Tessera.app" >&2
  exit 1
fi

codesign --force --sign - "$APP"
codesign --verify --strict --verbose=2 "$APP"
echo "sign: pacote válido — $APP"
