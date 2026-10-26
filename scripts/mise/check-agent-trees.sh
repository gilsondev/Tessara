#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
test -d "$ROOT/agentd/crates/agentd" || { echo "missing agentd/"; exit 1; }
echo "agentd/: ok"
test -d "$ROOT/apps/agent-ui" || { echo "missing apps/agent-ui/"; exit 1; }
echo "apps/agent-ui/: ok"
if [[ -d "$ROOT/agent" ]]; then
  echo "agent/: still present (legacy Python — remove before merge)" >&2
  exit 1
fi
echo "agent/ (legacy Python): gone"
