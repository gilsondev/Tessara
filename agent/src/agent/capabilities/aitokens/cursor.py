"""Cursor plan usage for the Tessera panel (Mac-side collector).

O Settings do Cursor mostra *dois* pools em **% usados** (não restante):

- Cursor Models ← `planUsage.autoPercentUsed`
- Other Models  ← `planUsage.apiPercentUsed`

Credenciais (mesma ordem usada por OpenUsage / onWatch / Continuum):

1. IDE SQLite `state.vscdb` (`cursorAuth/accessToken` + `refreshToken`)
2. macOS Keychain (`cursor-access-token` / `cursor-refresh-token`)
3. `~/.cursor/auth.json` (cursor-agent / `AGENT_CLI_CREDENTIAL_STORE=file`)
4. `CURSOR_API_KEY` (`crsr_…`) → exchange, ou `CURSOR_TOKEN` (Bearer pronto)

Access JWT é curto. Renova via `POST /oauth/token` antes de expirar / após 401.
Se o refresh voltar `shouldLogout`, tenta a próxima fonte — só o login no Cursor regenera.
"""

from __future__ import annotations

import base64
import json
import os
import sqlite3
import subprocess
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path

STATE_DB = Path.home() / "Library/Application Support/Cursor/User/globalStorage/state.vscdb"
AUTH_JSON_CANDIDATES = (
    Path.home() / ".cursor" / "auth.json",
    Path.home() / ".config" / "cursor" / "auth.json",
)
USAGE_URL = "https://api2.cursor.sh/aiserver.v1.DashboardService/GetCurrentPeriodUsage"
PLAN_URL = "https://api2.cursor.sh/aiserver.v1.DashboardService/GetPlanInfo"
OAUTH_URL = "https://api2.cursor.sh/oauth/token"
EXCHANGE_URL = "https://api2.cursor.sh/auth/exchange_user_api_key"
OAUTH_CLIENT_ID = "KbZUR41cY7W6zRSdpSUJ7I7mLYBKOCmB"
_REFRESH_SKEW_S = 300
_KEYCHAIN_ACCESS = "cursor-access-token"
_KEYCHAIN_REFRESH = "cursor-refresh-token"
_KEYCHAIN_ACCOUNT = "cursor-user"

_MONTHS = ("jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez")

_last_skip_reason = ""


@dataclass
class _Session:
    access: str | None
    refresh: str | None
    source: str  # sqlite | keychain | auth_json | api_key | env
    membership: str | None = None


def _skip(reason: str) -> None:
    global _last_skip_reason
    if reason == _last_skip_reason:
        return
    _last_skip_reason = reason
    print(f"tessera-agent: cursor indisponível ({reason})", flush=True)


def _clear_skip() -> None:
    global _last_skip_reason
    _last_skip_reason = ""


def _jwt_claims(token: str) -> dict | None:
    try:
        parts = token.split(".")
        if len(parts) < 2:
            return None
        payload = parts[1] + "=" * ((4 - len(parts[1]) % 4) % 4)
        return json.loads(base64.urlsafe_b64decode(payload.encode("ascii")))
    except (ValueError, json.JSONDecodeError, OSError, TypeError):
        return None


def _jwt_exp(token: str) -> float | None:
    claims = _jwt_claims(token)
    if not claims or claims.get("exp") is None:
        return None
    try:
        return float(claims["exp"])
    except (TypeError, ValueError):
        return None


def _jwt_sub(token: str | None) -> str | None:
    if not token:
        return None
    claims = _jwt_claims(token)
    if not claims:
        return None
    sub = claims.get("sub")
    return str(sub) if sub else None


def _token_needs_refresh(token: str) -> bool:
    exp = _jwt_exp(token)
    if exp is None:
        return False
    return time.time() >= (exp - _REFRESH_SKEW_S)


def _db_get(key: str) -> str | None:
    if not STATE_DB.is_file():
        return None
    try:
        con = sqlite3.connect(f"file:{STATE_DB}?mode=ro", uri=True)
        try:
            row = con.execute("SELECT value FROM ItemTable WHERE key = ?", (key,)).fetchone()
        finally:
            con.close()
    except sqlite3.Error:
        return None
    if not row or not row[0]:
        return None
    val = row[0]
    return val.decode("utf-8") if isinstance(val, bytes) else str(val)


def _db_set(key: str, value: str) -> None:
    if not STATE_DB.is_file() or not value:
        return
    try:
        con = sqlite3.connect(str(STATE_DB))
        try:
            con.execute(
                "INSERT INTO ItemTable(key, value) VALUES(?, ?) "
                "ON CONFLICT(key) DO UPDATE SET value = excluded.value",
                (key, value),
            )
            con.commit()
        finally:
            con.close()
    except sqlite3.Error:
        pass


def _keychain_get(service: str) -> str | None:
    try:
        proc = subprocess.run(
            [
                "security",
                "find-generic-password",
                "-s",
                service,
                "-a",
                _KEYCHAIN_ACCOUNT,
                "-w",
            ],
            check=False,
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    if proc.returncode != 0:
        return None
    val = (proc.stdout or "").strip()
    return val or None


def _auth_json_session() -> _Session | None:
    for path in AUTH_JSON_CANDIDATES:
        if not path.is_file():
            continue
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        if not isinstance(data, dict):
            continue
        access = data.get("accessToken") or data.get("access_token")
        refresh = data.get("refreshToken") or data.get("refresh_token")
        api_key = data.get("apiKey") or data.get("api_key")
        if isinstance(access, str) and access:
            return _Session(
                access=access,
                refresh=refresh if isinstance(refresh, str) else None,
                source="auth_json",
            )
        if isinstance(api_key, str) and api_key.startswith("crsr_"):
            exchanged = _exchange_api_key(api_key)
            if exchanged:
                return exchanged
    return None


def _exchange_api_key(api_key: str) -> _Session | None:
    req = urllib.request.Request(EXCHANGE_URL, data=b"{}", method="POST")
    req.add_header("Authorization", f"Bearer {api_key}")
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=12) as resp:
            data = json.loads(resp.read().decode("utf-8"))
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, json.JSONDecodeError, OSError):
        return None
    access = data.get("accessToken") or data.get("access_token")
    refresh = data.get("refreshToken") or data.get("refresh_token")
    if not isinstance(access, str) or not access:
        return None
    return _Session(
        access=access,
        refresh=refresh if isinstance(refresh, str) else None,
        source="api_key",
    )


def _load_sessions() -> list[_Session]:
    """Ordered candidates. Prefer a paid Keychain session over a free SQLite ghost."""
    sessions: list[_Session] = []

    sqlite_access = _db_get("cursorAuth/accessToken")
    sqlite_refresh = _db_get("cursorAuth/refreshToken")
    membership = (_db_get("cursorAuth/stripeMembershipType") or "").strip().lower() or None
    sqlite_sess = None
    if sqlite_access or sqlite_refresh:
        sqlite_sess = _Session(sqlite_access, sqlite_refresh, "sqlite", membership)

    kc_access = _keychain_get(_KEYCHAIN_ACCESS)
    kc_refresh = _keychain_get(_KEYCHAIN_REFRESH)
    kc_sess = None
    if kc_access or kc_refresh:
        kc_sess = _Session(kc_access, kc_refresh, "keychain")

    # OpenUsage rule: free SQLite + different Keychain subject → prefer Keychain.
    if sqlite_sess and kc_sess and membership == "free":
        if _jwt_sub(sqlite_sess.access) and _jwt_sub(kc_sess.access) and _jwt_sub(sqlite_sess.access) != _jwt_sub(
            kc_sess.access
        ):
            sessions.append(kc_sess)
            sessions.append(sqlite_sess)
            sqlite_sess = None
            kc_sess = None

    if sqlite_sess:
        sessions.append(sqlite_sess)
    if kc_sess:
        sessions.append(kc_sess)

    auth_json = _auth_json_session()
    if auth_json:
        sessions.append(auth_json)

    env_token = (os.environ.get("CURSOR_TOKEN") or "").strip()
    if env_token:
        sessions.append(_Session(env_token, None, "env"))

    env_key = (os.environ.get("CURSOR_API_KEY") or "").strip()
    if env_key.startswith("crsr_"):
        exchanged = _exchange_api_key(env_key)
        if exchanged:
            sessions.append(exchanged)

    # Dedupe by (source, sub or access prefix) while keeping order.
    seen: set[str] = set()
    out: list[_Session] = []
    for sess in sessions:
        mark = f"{sess.source}:{_jwt_sub(sess.access) or (sess.access or '')[:24]}"
        if mark in seen:
            continue
        seen.add(mark)
        out.append(sess)

    # Prefer a still-fresh access token so a dead SQLite refresh doesn't run first.
    def _rank(sess: _Session) -> tuple[int, int]:
        if sess.access and not _token_needs_refresh(sess.access):
            return (0, 0)
        if sess.refresh:
            return (1, 0)
        if sess.access:
            return (2, 0)
        return (3, 0)

    out.sort(key=_rank)
    return out


def _persist_access(session: _Session, access: str, refresh: str | None = None) -> None:
    session.access = access
    if refresh:
        session.refresh = refresh
    if session.source == "sqlite":
        _db_set("cursorAuth/accessToken", access)
        if refresh:
            _db_set("cursorAuth/refreshToken", refresh)
    elif session.source == "auth_json":
        for path in AUTH_JSON_CANDIDATES:
            if not path.is_file():
                continue
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                if not isinstance(data, dict):
                    continue
                data["accessToken"] = access
                if refresh:
                    data["refreshToken"] = refresh
                path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
            except OSError:
                pass
            break
    # Keychain writes need user interaction / ACL — leave to Cursor / agent login.


def _refresh_access_token(session: _Session) -> str | None:
    if not session.refresh:
        return None
    body = json.dumps(
        {
            "grant_type": "refresh_token",
            "client_id": OAUTH_CLIENT_ID,
            "refresh_token": session.refresh,
        }
    ).encode("utf-8")
    req = urllib.request.Request(OAUTH_URL, data=body, method="POST")
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=12) as resp:
            data = json.loads(resp.read().decode("utf-8"))
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, json.JSONDecodeError, OSError):
        return None
    if data.get("shouldLogout"):
        return None
    access = data.get("access_token") or data.get("accessToken")
    if not isinstance(access, str) or not access:
        return None
    new_refresh = data.get("refresh_token") or data.get("refreshToken")
    _persist_access(session, access, new_refresh if isinstance(new_refresh, str) else None)
    print(f"tessera-agent: cursor token renovado via {session.source}", flush=True)
    return access


def _bearer(session: _Session) -> str | None:
    if session.access and not _token_needs_refresh(session.access):
        return session.access
    renewed = _refresh_access_token(session)
    if renewed:
        return renewed
    return session.access


def _post(url: str, token: str) -> tuple[dict | None, int | None]:
    req = urllib.request.Request(url, data=b"{}", method="POST")
    req.add_header("Authorization", f"Bearer {token}")
    req.add_header("Content-Type", "application/json")
    req.add_header("Connect-Protocol-Version", "1")
    try:
        with urllib.request.urlopen(req, timeout=12) as resp:
            return json.loads(resp.read().decode("utf-8")), resp.status
    except urllib.error.HTTPError as exc:
        return None, exc.code
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError):
        return None, None


def _post_session(url: str, session: _Session) -> dict | None:
    token = _bearer(session)
    if not token:
        return None
    data, status = _post(url, token)
    if data is not None:
        return data
    if status != 401:
        return None
    renewed = _refresh_access_token(session)
    if not renewed:
        return None
    data, _ = _post(url, renewed)
    return data


def _used_pct(raw) -> int | None:
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
    sessions = _load_sessions()
    if not sessions:
        _skip("sem sessão — abra o Cursor, rode `agent login`, ou defina CURSOR_API_KEY")
        return out

    last_fail = "nenhuma fonte de auth respondeu"
    for session in sessions:
        usage = _post_session(USAGE_URL, session)
        if not usage:
            last_fail = f"{session.source}: 401/refresh falhou"
            continue
        plan = usage.get("planUsage") or {}
        pools = _pools_used(plan)
        if pools is None:
            last_fail = f"{session.source}: sem autoPercentUsed/apiPercentUsed"
            continue

        _clear_skip()
        auto_used, api_used = pools
        rem = max(0, 100 - max(auto_used, api_used))
        period = None
        cycle_end = usage.get("billingCycleEnd")
        plan_info = _post_session(PLAN_URL, session)
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
                "display": f"{auto_used}% · {api_used}%",
                "reset": _cycle_reset_label(cycle_end, period),
            }
        )
        return out

    _skip(last_fail)
    return out
