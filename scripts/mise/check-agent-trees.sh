#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
test -d "$ROOT/agent/src/agent" || { echo "missing legacy agent/"; exit 1; }
echo "legacy agent/: ok"
if [[ -d "$ROOT/agentd" ]]; then
  echo "agentd/: present"
else
  echo "agentd/: not created yet (expected until daemon plan)"
fi
if [[ -d "$ROOT/apps/agent-ui" ]]; then
  echo "apps/agent-ui/: present"
else
  echo "apps/agent-ui/: not created yet (expected until ui plan)"
fi
