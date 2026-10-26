"""Google Calendar capability — OAuth no Mac, snapshot/catalog no USB."""

from __future__ import annotations

import json
import re
import threading
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any
from zoneinfo import ZoneInfo

SCOPE = "https://www.googleapis.com/auth/calendar.readonly"
TZ = ZoneInfo("America/Sao_Paulo")
ROOT = Path.home() / ".tessera" / "calendar"
ACCOUNTS_DIR = ROOT / "accounts"
CLIENT_PATH = ROOT / "client.json"
SELECTION_PATH = ROOT / "selection.json"

_auth_lock = threading.Lock()
_auth_pending = False
_auth_status = ""
_selected: list[str] | None = None


def _safe_email(email: str) -> str:
    return re.sub(r"[^a-zA-Z0-9._@+-]", "_", email)[:80]


def ensure_dirs() -> None:
    ACCOUNTS_DIR.mkdir(parents=True, exist_ok=True)


def load_selection() -> list[str]:
    global _selected
    if _selected is not None:
        return list(_selected)
    if SELECTION_PATH.is_file():
        try:
            data = json.loads(SELECTION_PATH.read_text())
            if isinstance(data, list):
                _selected = [str(x) for x in data]
                return list(_selected)
        except (OSError, json.JSONDecodeError):
            pass
    _selected = []
    return []


def set_selection(ids: list[str]) -> None:
    global _selected
    _selected = [str(x) for x in ids if x]
    ensure_dirs()
    SELECTION_PATH.write_text(json.dumps(_selected))


def account_files() -> list[Path]:
    ensure_dirs()
    return sorted(ACCOUNTS_DIR.glob("*.json"))


def _load_client() -> dict[str, Any] | None:
    if not CLIENT_PATH.is_file():
        return None
    try:
        return json.loads(CLIENT_PATH.read_text())
    except (OSError, json.JSONDecodeError):
        return None


def _creds_from_file(path: Path):
    from google.auth.transport.requests import Request
    from google.oauth2.credentials import Credentials

    data = json.loads(path.read_text())
    creds = Credentials.from_authorized_user_info(data, [SCOPE])
    if creds and creds.expired and creds.refresh_token:
        creds.refresh(Request())
        path.write_text(creds.to_json())
    return creds


def list_accounts_calendars() -> list[dict[str, Any]]:
    """[{email, calendars:[{id,name,color}]}]"""
    try:
        from googleapiclient.discovery import build
    except ImportError:
        return []

    out: list[dict[str, Any]] = []
    for path in account_files():
        try:
            creds = _creds_from_file(path)
            if not creds or not creds.valid:
                continue
            service = build("calendar", "v3", credentials=creds, cache_discovery=False)
            email = path.stem.replace("_at_", "@")
            # Prefer email from token info / primary
            about = service.calendarList().get(calendarId="primary").execute()
            email = about.get("id") or about.get("summary") or email
            cals = []
            page = None
            while True:
                resp = service.calendarList().list(maxResults=50, pageToken=page).execute()
                for item in resp.get("items", []):
                    cid = item.get("id") or ""
                    if not cid:
                        continue
                    color = (item.get("backgroundColor") or "#1A73E8").lstrip("#")
                    cals.append(
                        {
                            "id": cid,
                            "name": item.get("summary") or cid,
                            "color": color[:6],
                        }
                    )
                page = resp.get("nextPageToken")
                if not page:
                    break
            out.append({"email": email, "calendars": cals, "path": str(path)})
        except Exception:
            continue
    return out


# USB console line on device is 2048; keep chunks comfortably under that.
_CATALOG_CHUNK_MAX = 900


def build_catalog_lines() -> list[str]:
    """One or more CALENDAR_CATALOG lines (status ok, then + chunks)."""
    selected = set(load_selection())
    accounts = list_accounts_calendars()
    if not accounts:
        status = "sem conta" if _load_client() else "configure client.json"
        return [f"CALENDAR_CATALOG\tv1\t0\t{status}"]

    # Always keep each account's primary (id == email) selected.
    for acc in accounts:
        email = acc.get("email") or ""
        if email:
            selected.add(email)
        for cal in acc.get("calendars") or []:
            if cal.get("id") in (email, "primary"):
                selected.add(cal["id"])
    if not selected and accounts and accounts[0]["calendars"]:
        selected.add(accounts[0]["calendars"][0]["id"])
    set_selection(sorted(selected))

    entries: list[list[str]] = []
    for acc in accounts:
        email = acc["email"].replace("\t", " ")
        for cal in acc["calendars"]:
            sel = "1" if cal["id"] in selected else "0"
            name = cal["name"].replace("\t", " ").replace("\r", " ").replace("\n", " ")
            # Primary calendar often shares the email as summary — show a clear label.
            if cal["id"] in (email, "primary") or name.lower() == email.lower():
                name = "Principal"
            # Guard against USB line-merge artifacts in display names.
            for junk in ("AITOKENS", "AITOKEN", "CALENDAR"):
                if junk in name:
                    name = name.split(junk, 1)[0]
            name = name.strip()[:39] or cal["id"][:39]
            entries.append(
                [
                    email,
                    cal["id"].replace("\t", " "),
                    name,
                    cal["color"],
                    sel,
                ]
            )

    lines: list[str] = []
    chunk: list[str] = []
    first = True

    def flush() -> None:
        nonlocal chunk, first
        if not chunk:
            return
        status = "ok" if first else "+"
        first = False
        lines.append("\t".join(["CALENDAR_CATALOG", "v1", "1", status, *chunk]))
        chunk = []

    for fields in entries:
        candidate = "\t".join(["CALENDAR_CATALOG", "v1", "1", "ok" if first else "+", *chunk, *fields])
        if chunk and len(candidate) > _CATALOG_CHUNK_MAX:
            flush()
        chunk.extend(fields)
    flush()
    return lines or ["CALENDAR_CATALOG\tv1\t1\tok"]


def build_catalog_line() -> str:
    """Single-line helper (tests / debug); prefer build_catalog_lines() on the wire."""
    return build_catalog_lines()[0]


def _hhmm(dt: datetime) -> str:
    local = dt.astimezone(TZ)
    return f"{local.hour:02d}{local.minute:02d}"


def build_events_line() -> str:
    try:
        from googleapiclient.discovery import build
    except ImportError:
        return "CALENDAR\tv1\t0\t0"

    accounts = list_accounts_calendars()
    linked = 1 if accounts else 0
    if not accounts:
        return f"CALENDAR\tv1\t1\t{linked}"

    selected = set(load_selection())
    # Always include each account's primary calendar (id == email).
    for acc in accounts:
        email = acc.get("email") or ""
        if email:
            selected.add(email)
        for cal in acc.get("calendars") or []:
            if cal.get("id") in (email, "primary"):
                selected.add(cal["id"])
    if selected != set(load_selection()):
        set_selection(sorted(selected))

    if not selected:
        return "CALENDAR\tv1\t1\t1"

    now = datetime.now(TZ)
    day_start = now.replace(hour=0, minute=0, second=0, microsecond=0)
    # Today only — no tomorrow onwards on the panel.
    horizon = day_start + timedelta(days=1)
    time_min = day_start.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")
    time_max = horizon.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")

    events: list[dict[str, Any]] = []
    for acc in accounts:
        try:
            creds = _creds_from_file(Path(acc["path"]))
            service = build("calendar", "v3", credentials=creds, cache_discovery=False)
        except Exception:
            continue
        color_by_id = {c["id"]: c["color"] for c in acc["calendars"]}
        for cal_id in selected:
            try:
                resp = (
                    service.events()
                    .list(
                        calendarId=cal_id,
                        timeMin=time_min,
                        timeMax=time_max,
                        singleEvents=True,
                        orderBy="startTime",
                        maxResults=20,
                    )
                    .execute()
                )
            except Exception:
                continue
            for item in resp.get("items", []):
                start = item.get("start", {})
                end = item.get("end", {})
                all_day = "date" in start and "dateTime" not in start
                if all_day:
                    try:
                        s_local = datetime.fromisoformat(start["date"]).replace(tzinfo=TZ)
                    except ValueError:
                        continue
                    # Single-day all-day for today only (Google exclusive end = tomorrow).
                    if s_local.date() != day_start.date():
                        continue
                    try:
                        e_date = datetime.fromisoformat(end["date"]).date()
                    except (KeyError, ValueError, TypeError):
                        e_date = day_start.date() + timedelta(days=1)
                    if e_date != day_start.date() + timedelta(days=1):
                        continue
                    start_min = 0
                    end_min = 24 * 60
                    dur = 0
                    day_offset = 0
                else:
                    try:
                        sdt = datetime.fromisoformat(start["dateTime"].replace("Z", "+00:00"))
                        edt = datetime.fromisoformat(end["dateTime"].replace("Z", "+00:00"))
                    except (KeyError, ValueError):
                        continue
                    s_local = sdt.astimezone(TZ)
                    day_offset = (s_local.date() - day_start.date()).days
                    if day_offset != 0:
                        continue
                    if edt <= now.astimezone(timezone.utc):
                        continue  # already finished today
                    start_min = s_local.hour * 60 + s_local.minute
                    end_min = edt.astimezone(TZ).hour * 60 + edt.astimezone(TZ).minute
                    dur = max(0, int((edt - sdt).total_seconds() // 60))
                title = (item.get("summary") or "Evento").replace("\t", " ")[:47]
                loc = (item.get("location") or "").replace("\t", " ")[:31]
                color = color_by_id.get(cal_id, "1A73E8")
                events.append(
                    {
                        "day": day_offset,
                        "start": start_min,
                        "end": end_min,
                        "all_day": all_day,
                        "title": title,
                        "loc": loc,
                        "dur": dur,
                        "color": color,
                        "sort": (day_offset, 0 if all_day else start_min),
                    }
                )

    events.sort(key=lambda e: (e["sort"], e["title"]))
    events = events[:12]
    parts = ["CALENDAR", "v1", "1", "1"]
    for e in events:
        if e["all_day"]:
            start_s, end_s, ad = "0000", "0000", "1"
        else:
            start_s = f"{e['start'] // 60:02d}{e['start'] % 60:02d}"
            end_s = f"{e['end'] // 60:02d}{e['end'] % 60:02d}"
            ad = "0"
        parts.extend(
            [start_s, end_s, ad, e["title"], e["loc"], str(e["dur"]), e["color"], str(e["day"])]
        )
    return "\t".join(parts)


def auth_status_line() -> str:
    pending = "1" if _auth_pending else "0"
    status = (_auth_status or ("autorizando…" if _auth_pending else "")).replace("\t", " ")[:40]
    return f"CALENDAR_AUTH\tv1\t{pending}\t{status}"


def start_auth_async() -> list[str]:
    """Kick OAuth in a background thread; return immediate status lines."""
    global _auth_pending, _auth_status
    if not _load_client():
        _auth_status = "falta ~/.tessera/calendar/client.json"
        return [
            "CALENDAR_AUTH\tv1\t0\tfalta client.json",
            *build_catalog_lines(),
        ]
    with _auth_lock:
        if _auth_pending:
            return [auth_status_line()]
        _auth_pending = True
        _auth_status = "abra o browser no Mac"

    def run():
        global _auth_pending, _auth_status
        try:
            from google_auth_oauthlib.flow import InstalledAppFlow

            client = _load_client()
            flow = InstalledAppFlow.from_client_config(client, [SCOPE])
            creds = flow.run_local_server(port=0, prompt="consent")
            ensure_dirs()
            # Resolve email
            email = "account"
            try:
                from googleapiclient.discovery import build

                service = build("calendar", "v3", credentials=creds, cache_discovery=False)
                about = service.calendarList().get(calendarId="primary").execute()
                email = about.get("id") or "account"
            except Exception:
                email = f"account_{int(time.time())}"
            path = ACCOUNTS_DIR / f"{_safe_email(email)}.json"
            path.write_text(creds.to_json())
            _auth_status = f"ligado {email}"
        except Exception as exc:
            _auth_status = str(exc)[:40].replace("\t", " ")
        finally:
            _auth_pending = False

    threading.Thread(target=run, daemon=True).start()
    return [auth_status_line()]


def handle_line(text: str) -> list[str]:
    """USB commands from the panel → response lines."""
    if text.startswith("CALENDAR_AUTH?"):
        return start_auth_async()
    if text.startswith("CALENDAR_CATALOG?"):
        return build_catalog_lines()
    if text.startswith("CALENDAR_SEL\t"):
        ids = [p for p in text.split("\t")[1:] if p]
        set_selection(ids)
        # Events first — USB drops mid-burst; agenda tile needs CALENDAR before catalog.
        return [build_events_line(), *build_catalog_lines()]
    if text.startswith("CALENDAR?"):
        lines = [build_events_line(), *build_catalog_lines()]
        if _auth_pending:
            lines.insert(0, auth_status_line())
        return lines
    return []


_push_n = 0


def push_lines() -> list[str]:
    # Events every tick; catalog less often (USB drops mid-burst and corrupts rows).
    global _push_n
    _push_n += 1
    lines = [build_events_line()]
    if (account_files() or _auth_status) and _push_n % 3 == 1:
        lines.extend(build_catalog_lines())
    if _auth_pending or _auth_status:
        lines.insert(0, auth_status_line())
    return lines
