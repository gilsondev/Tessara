"""Cursor plan usage via local IDE token.

O Settings do Cursor mostra *dois* pools em **% usados** (não restante):

- Cursor Models (Grok/Composer) ← `planUsage.autoPercentUsed`  → ex. "3% used"
- Other Models                  ← `planUsage.apiPercentUsed`   → ex. "13% used"

NÃO usar:
- `totalPercentUsed` — métrica agregada, não é nenhuma das barras
- `displayMessage` ("You've used 61%…") — não aparece nessas barras do Settings

No protocolo Tessera (que usa % restante p/ alertas):
- `display` = "3% · 13%" (usados, na ordem Modelos · Outros — igual ao Settings)
- `value`   = restante do pior pool = 100 - max(usados)  (alertas / borda)
- `reset`   = data do fim do ciclo (`billingCycleEnd`)
"""

from __future__ import annotations

import json
import sqlite3
import time
import urllib.error
import urllib.request
from pathlib import Path

STATE_DB = Path.home() / "Library/Application Support/Cursor/User/globalStorage/state.vscdb"
USAGE_URL = "https://api2.cursor.sh/aiserver.v1.DashboardService/GetCurrentPeriodUsage"
PLAN_URL = "https://api2.cursor.sh/aiserver.v1.DashboardService/GetPlanInfo"

_MONTHS = ("jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez")


def _access_token() -> str | None:
    if not STATE_DB.is_file():
        return None
    try:
        con = sqlite3.connect(f"file:{STATE_DB}?mode=ro", uri=True)
        try:
            row = con.execute(
                "SELECT value FROM ItemTable WHERE key = ?",
                ("cursorAuth/accessToken",),
            ).fetchone()
        finally:
            con.close()
    except sqlite3.Error:
        return None
    if not row or not row[0]:
        return None
    val = row[0]
    return val.decode("utf-8") if isinstance(val, bytes) else str(val)


def _post(url: str, token: str) -> dict | None:
    req = urllib.request.Request(url, data=b"{}", method="POST")
    req.add_header("Authorization", f"Bearer {token}")
    req.add_header("Content-Type", "application/json")
    req.add_header("Connect-Protocol-Version", "1")
    try:
        with urllib.request.urlopen(req, timeout=12) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, json.JSONDecodeError, OSError):
        return None


def _used_pct(raw) -> int | None:
    """Cursor Settings trunca (3.35 → 3%, 13.28 → 13%)."""
    if raw is None:
        return None
    try:
        return max(0, min(100, int(float(raw))))
    except (TypeError, ValueError):
        return None


def _pools_used(plan: dict) -> tuple[int, int] | None:
    if not plan:
        return None
    auto = _used_pct(plan.get("autoPercentUsed"))
    api = _used_pct(plan.get("apiPercentUsed"))
    if auto is None or api is None:
        return None
    return auto, api


def _ts(ms) -> float | None:
    if ms is None or ms == "":
        return None
    try:
        value = float(ms)
    except (TypeError, ValueError):
        return None
    return value / 1000.0 if value > 1e12 else value


def _cycle_reset_label(ms, period: str | None) -> str:
    ts = _ts(ms)
    if ts is None:
        return ""
    local = time.localtime(ts)
    day = local.tm_mday
    mon = _MONTHS[local.tm_mon - 1]
    if period and "WEEKLY" in period.upper():
        return f"{day:02d} {mon}"
    return f"{day:02d} {mon}"


def read() -> dict:
    out = {
        "id": "cursor",
        "available": False,
        "kind": "none",
        "value": 0,
        "display": "—",
        "reset": "",
    }
    token = _access_token()
    if not token:
        return out

    usage = _post(USAGE_URL, token)
    if not usage:
        return out
    plan = usage.get("planUsage") or {}
    pools = _pools_used(plan)
    if pools is None:
        return out
    auto_used, api_used = pools
    # value = % restante do pior pool (alertas iguais às outras fontes)
    rem = max(0, 100 - max(auto_used, api_used))

    period = None
    cycle_end = usage.get("billingCycleEnd")
    plan_info = _post(PLAN_URL, token)
    if plan_info:
        info = plan_info.get("planInfo") or {}
        period = info.get("includedUsagePeriod")
        if info.get("billingCycleEnd"):
            cycle_end = info["billingCycleEnd"]
        if info.get("includedUsageResetsAt"):
            cycle_end = info["includedUsageResetsAt"]

    out.update(
        {
            "available": True,
            "kind": "pct",
            "value": rem,
            # Ordem = Settings: Cursor Models · Other Models (ambos % usados)
            "display": f"{auto_used}% · {api_used}%",
            "reset": _cycle_reset_label(cycle_end, period),
        }
    )
    return out
