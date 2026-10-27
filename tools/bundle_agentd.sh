#!/bin/bash
# Copy the release agent into the app resources so Tessera.app can install it
# on first open. Run after `cargo build --release -p agentd` and before
# `tauri build`.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/agentd/target/release/Tessera"
DEST_DIR="$ROOT/apps/agent-ui/src-tauri/resources"
DEST="$DEST_DIR/agentd"

if [ ! -x "$SRC" ]; then
  echo "bundle-agentd: falta $SRC — rode cargo build --release -p agentd antes" >&2
  exit 1
fi

mkdir -p "$DEST_DIR"
install -m 755 "$SRC" "$DEST"
echo "bundle-agentd: $DEST"
