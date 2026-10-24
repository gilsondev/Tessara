"""Read Claude Code rate limits from the statusLine JSON file (% restante)."""

from __future__ import annotations

import json
import time
from pathlib import Path

STATUS_PATH = Path.home() / ".tessera" / "claude_status.json"
MAX_AGE_S = 120


def read() -> dict:
    out = {
        "id": "claude",
        "available": False,
        "kind": "none",
        "value": 0,
        "display": "—",
        "reset": "",
    }
    if not STATUS_PATH.is_file():
        return out
    try:
        data = json.loads(STATUS_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return out
    generated = data.get("generated_at")
    if isinstance(generated, (int, float)) and time.time() - float(generated) > MAX_AGE_S:
        return out
    pct_used = data.get("percent_used")
    if pct_used is None:
        return out
    rem = max(0, min(100, int(round(100 - float(pct_used)))))
    reset = data.get("reset_label") or ""
    out.update(
        {
            "available": True,
            "kind": "pct",
            "value": rem,
            "display": f"{rem}%",
            "reset": str(reset)[:15],
        }
    )
    return out
