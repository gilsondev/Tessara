#!/usr/bin/env bash
# Acesso ao painel físico para teste manual / ver telas.
# Uso:
#   ./tools/device.sh           # monitor serial + dicas
#   ./tools/device.sh monitor   # só monitor
#   ./tools/device.sh snap [path.png]  # captura framebuffer (SNAP)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PIO="${ROOT}/.venv/bin/pio"
PY="${ROOT}/.venv/bin/python"
OUT="${SIM_OUT:-/tmp/tessara-sim-out}"
CMD="${1:-monitor}"

app_port() {
  "$PY" - <<'PY'
import glob
ports = sorted(p for p in glob.glob("/dev/cu.usbmodem*") if not p.endswith("1401"))
print(ports[0] if ports else "")
PY
}

PORT="$(app_port)"
if [[ -z "$PORT" ]]; then
  echo "painel ausente: plugue o USB (porta de app, não bootloader …1401)" >&2
  exit 1
fi

case "$CMD" in
  monitor|"")
    echo "==> painel em $PORT"
    echo ""
    echo "Teste manual — olhe a tela do JC3248:"
    echo "  • Home (tiles Tarefas / Clima / AI Tokens)"
    echo "  • Engrenagem → Configurações (Geral, descanso, duração do relógio, …)"
    echo "  • Detalhe Todoist (chips + slide), Clima, AI Tokens"
    echo "  • Portal: http://tessera.local/  (ou IP do card Wi‑Fi nos ajustes)"
    echo ""
    echo "Serial 115200 — Ctrl+C para sair. Logs do firmware abaixo."
    echo ""
    # Note: older PIO had --no-reset; current PlatformIO 6.x does not.
    exec "$PIO" device monitor -e jc3248w535 --port "$PORT" --baud 115200 \
      --filter time --echo
    ;;
  snap)
    mkdir -p "$OUT"
    DEST="${2:-$OUT/device-snap.png}"
    echo "==> SNAP $PORT → $DEST"
    "$PY" - <<PY
import sys
sys.path.insert(0, "${ROOT}/tools")
from panel_probe import open_port, snapshot
ser = open_port()
try:
    snapshot(ser, "${DEST}")
finally:
    ser.close()
PY
    echo "abra: $DEST"
    ;;
  port)
    echo "$PORT"
    ;;
  *)
    echo "uso: $0 [monitor|snap [path.png]|port]" >&2
    exit 2
    ;;
esac
