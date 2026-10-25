"""MEDIA capability — Now Playing push + play/pause/prev/next."""

from __future__ import annotations

from agent.capabilities.media import controls, nowplaying

_last_line = ""


def _sanitize(text: str, limit: int) -> str:
    if not text:
        return ""
    cleaned = text.replace("\t", " ").replace("\n", " ").replace("\r", " ").strip()
    if len(cleaned) > limit:
        cleaned = cleaned[: limit - 1] + "…"
    return cleaned


def encode_line(
    ok: bool,
    state: str = "stopped",
    title: str = "",
    artist: str = "",
    app: str = "",
) -> str:
    if not ok:
        return "MEDIA\tv1\t0\t\t\t\t"
    st = state if state in ("playing", "paused", "stopped") else "stopped"
    return (
        f"MEDIA\tv1\t1\t{st}\t{_sanitize(title, 40)}\t{_sanitize(artist, 40)}\t{_sanitize(app, 24)}"
    )


def build_line() -> str:
    global _last_line
    info = nowplaying.read()
    if info is None:
        line = encode_line(False)
    else:
        line = encode_line(True, info.state, info.title, info.artist, info.app)
    _last_line = line
    return line


def handle_line(text: str) -> list[str]:
    if not text:
        return []
    if text.startswith("MEDIA?"):
        return [build_line()]
    if text == "MEDIA\tPLAYPAUSE":
        controls.playpause()
        return [build_line()]
    if text == "MEDIA\tNEXT":
        controls.next_track()
        return [build_line()]
    if text == "MEDIA\tPREV":
        controls.prev_track()
        return [build_line()]
    return []
