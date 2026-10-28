#!/bin/bash
# Claude Code statusLine → ~/.tessera/claude_status.json
# In ~/.claude/settings.json:
#   "statusLine": { "type": "command", "command": "/ABS/PATH/statusline_claude.sh" }
set -euo pipefail
mkdir -p "$HOME/.tessera"
python3 /dev/fd/3 3<<'PY'
import json, sys, time
from pathlib import Path

raw = sys.stdin.read()
try:
    data = json.loads(raw) if raw.strip() else {}
except json.JSONDecodeError:
    data = {}

limits = data.get("rate_limits") or {}
five = limits.get("five_hour") or {}
seven = limits.get("seven_day") or {}
chosen = five if five.get("used_percentage") is not None else seven
pct = chosen.get("used_percentage")
resets = chosen.get("resets_at")
label = ""
if isinstance(resets, (int, float)):
    label = time.strftime("%H:%M", time.localtime(resets))

out = {
    "generated_at": time.time(),
    "percent_used": pct,
    "display_value": f"{int(pct)}%" if pct is not None else "",
    "reset_label": label,
    "source": "claude_statusline",
}
Path.home().joinpath(".tessera/claude_status.json").write_text(
    json.dumps(out), encoding="utf-8"
)
# Optional: print a short status for the Claude TUI
if out["display_value"]:
    print(f"5h {out['display_value']}" + (f" · {label}" if label else ""))
PY
