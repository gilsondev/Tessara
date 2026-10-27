#!/usr/bin/env bash
# Build agentd + Tessera.app and install locally (LaunchAgent + /Applications).
# Uso: tools/agent-install.sh
# Não depende de agente de IA — rode após mudanças no companion.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# Cursor and CI sandboxes sometimes point Cargo at a cache. The install paths
# below are repo-relative, so the binaries have to land in agentd/target.
unset CARGO_TARGET_DIR

echo "agent-install: building agentd…"
(cd agentd && cargo build --release -p agentd)

echo "agent-install: building Tessera.app…"
(cd apps/agent-ui && npm install --silent && npm run tauri build)

# UI may overwrite agentd/target/release/Tessera — rebuild daemon last.
echo "agent-install: rebuilding agentd binary (name collision with UI)…"
(cd agentd && cargo build --release -p agentd)

APP=""
for cand in \
  "$ROOT/apps/agent-ui/src-tauri/target/release/bundle/macos/Tessera.app" \
  "$ROOT/agentd/target/release/bundle/macos/Tessera.app"; do
  if [ -d "$cand" ]; then APP="$cand"; break; fi
done
if [ -z "$APP" ]; then
  echo "agent-install: Tessera.app não encontrado após tauri build" >&2
  exit 1
fi
bash "$ROOT/tools/sign_macos_app.sh" "$APP"

export TESSERA_APP="$APP"
export AGENTD_BIN="$ROOT/agentd/target/release/Tessera"
exec "$ROOT/tools/cutover_agent_v2.sh"
