"""Read Now Playing from Music / Spotify (osascript). Degrades to None on failure."""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from typing import Optional


@dataclass
class NowPlaying:
    state: str  # playing | paused | stopped
    title: str
    artist: str
    app: str


def read() -> Optional[NowPlaying]:
    script = r"""
    set titleText to ""
    set artistText to ""
    set appName to ""
    set playState to "stopped"
    try
      if application "Music" is running then
        tell application "Music"
          if player state is playing then
            set playState to "playing"
          else if player state is paused then
            set playState to "paused"
          end if
          if player state is not stopped then
            set titleText to name of current track
            set artistText to artist of current track
            set appName to "Music"
          end if
        end tell
      end if
    end try
    if titleText is "" then
      try
        if application "Spotify" is running then
          tell application "Spotify"
            if player state is playing then
              set playState to "playing"
            else if player state is paused then
              set playState to "paused"
            end if
            if player state is not stopped then
              set titleText to name of current track
              set artistText to artist of current track
              set appName to "Spotify"
            end if
          end tell
        end if
      end try
    end if
    return playState & tab & titleText & tab & artistText & tab & appName
    """
    try:
        out = subprocess.run(
            ["osascript", "-e", script],
            capture_output=True,
            text=True,
            timeout=1.5,
            check=False,
        )
        if out.returncode != 0:
            return None
        line = (out.stdout or "").rstrip("\n")
        parts = line.split("\t")
        while len(parts) < 4:
            parts.append("")
        state, title, artist, app = parts[0], parts[1], parts[2], parts[3]
        if state not in ("playing", "paused", "stopped"):
            state = "stopped"
        return NowPlaying(state=state, title=title, artist=artist, app=app)
    except (OSError, subprocess.TimeoutExpired):
        return None
