#!/usr/bin/env bash
# Finish gate via Makefile (fonte de verdade das tasks).
# Uso: ./tools/finish.sh
#      SKIP_FLASH=1 ./tools/finish.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if [[ "${SKIP_FLASH:-}" == "1" ]]; then
  exec make finish-host
fi
exec make finish
