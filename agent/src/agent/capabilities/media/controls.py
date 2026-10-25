"""Transport controls for the current Now Playing app (Music / Spotify)."""

from __future__ import annotations

import subprocess

from agent.capabilities.media import nowplaying


def _tell(app: str, verb: str) -> bool:
    try:
        r = subprocess.run(
            ["osascript", "-e", f'tell application "{app}" to {verb}'],
            capture_output=True,
            timeout=3,
            check=False,
        )
        return r.returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def _target_app() -> str:
    info = nowplaying.read()
    if info and info.app in ("Music", "Spotify"):
        return info.app
    # Prefer whichever is running with a session.
    for app in ("Music", "Spotify"):
        try:
            r = subprocess.run(
                ["osascript", "-e", f'application "{app}" is running'],
                capture_output=True,
                text=True,
                timeout=2,
                check=False,
            )
            if r.returncode == 0 and "true" in (r.stdout or "").lower():
                return app
        except (OSError, subprocess.TimeoutExpired):
            continue
    return "Music"


def playpause() -> bool:
    return _tell(_target_app(), "playpause")


def next_track() -> bool:
    return _tell(_target_app(), "next track")


def prev_track() -> bool:
    return _tell(_target_app(), "previous track")
