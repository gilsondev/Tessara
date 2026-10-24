#!/usr/bin/env bash
# Create .venv, install PlatformIO, fetch jc3248w535 lib deps (LVGL, …).
# OS packages (SDL2, CMake, …) must already be installed — see docs/getting-started.md
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v python3 >/dev/null 2>&1; then
  echo "error: python3 not found — install Python 3.10+ (see docs/getting-started.md)" >&2
  exit 1
fi

if [[ ! -d .venv ]]; then
  echo "==> creating .venv"
  python3 -m venv .venv
fi

echo "==> installing PlatformIO"
.venv/bin/pip install -U pip platformio

echo "==> pio pkg install -e jc3248w535 (LVGL / ArduinoJson for sim + device)"
.venv/bin/pio pkg install -e jc3248w535

echo "==> checking host packages (best-effort)"
missing=0
for cmd in cmake ninja pkg-config; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "  missing: $cmd"
    missing=1
  fi
done
if ! pkg-config --exists sdl2 2>/dev/null; then
  echo "  missing: sdl2 (pkg-config)"
  missing=1
fi
if ! pkg-config --exists freetype2 2>/dev/null; then
  echo "  missing: freetype2 (pkg-config)"
  missing=1
fi

if [[ "$missing" -ne 0 ]]; then
  echo "Install OS packages from docs/getting-started.md, then re-run."
  exit 1
fi

echo "==> bootstrap OK"
echo "    next: make finish-host"
echo "          make sim"
