#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT/agent"
if [[ ! -d .venv ]]; then
  python -m venv .venv
  .venv/bin/pip install -r requirements.txt
fi
export PYTHONPATH=src
exec .venv/bin/python -m agent "$@"
