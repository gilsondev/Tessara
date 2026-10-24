"""Codex / ChatGPT rate limits via local auth → wham/usage.

Dois recortes (como o ChatGPT mostra):
- primary_window  → últimas ~5 h  (% restante)
- secondary_window → semana       (% restante)

No protocolo:
- display = "100% · 88%"  (5h · semana, ambos restantes)
- value   = restante das 5 h (alertas)
- reset   = horário do reset da janela de 5 h (HH:MM)
"""

from __future__ import annotations

import json
import time
import urllib.error
import urllib.request
from pathlib import Path

AUTH_PATH = Path.home() / ".codex" / "auth.json"
USAGE_URL = "https://chatgpt.com/backend-api/wham/usage"


def _remaining(used: int | float | None) -> int | None:
    if used is None:
        return None
    return max(0, min(100, int(round(100 - float(used)))))


def _reset_label(reset_at) -> str:
    if not isinstance(reset_at, (int, float)) or reset_at <= 0:
        return ""
    return time.strftime("%H:%M", time.localtime(reset_at))


def read() -> dict:
    out = {
        "id": "codex",
        "available": False,
        "kind": "none",
        "value": 0,
        "display": "—",
        "reset": "",
    }
    if not AUTH_PATH.is_file():
        return out
    try:
        auth = json.loads(AUTH_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return out
    tokens = auth.get("tokens") or {}
    access = tokens.get("access_token")
    account = tokens.get("account_id") or ""
    if not access:
        return out
    req = urllib.request.Request(USAGE_URL)
    req.add_header("Authorization", f"Bearer {access}")
    if account:
        req.add_header("ChatGPT-Account-Id", account)
    req.add_header("User-Agent", "TessaraAiTokensBridge/1.0")
    try:
        with urllib.request.urlopen(req, timeout=12) as resp:
            data = json.loads(resp.read().decode("utf-8"))
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, json.JSONDecodeError, OSError):
        return out

    rate = data.get("rate_limit") or {}
    primary = rate.get("primary_window") or {}
    secondary = rate.get("secondary_window") or {}
    rem5 = _remaining(primary.get("used_percent"))
    rem_week = _remaining(secondary.get("used_percent"))
    if rem5 is None and rem_week is None:
        return out
    if rem5 is None:
        rem5 = rem_week
    if rem_week is None:
        rem_week = rem5

    out.update(
        {
            "available": True,
            "kind": "pct",
            "value": rem5,
            "display": f"{rem5}% · {rem_week}%",
            "reset": _reset_label(primary.get("reset_at") or secondary.get("reset_at")),
        }
    )
    return out
