#!/usr/bin/env python3
"""Print legacy AGENT/MEDIA lines for golden fixtures (stdout only)."""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "agent" / "src"))

from agent.capabilities.focus import encode_line as focus_encode  # noqa: E402
from agent.capabilities.media import encode_line  # noqa: E402
from agent.protocol import agent_hello  # noqa: E402


def main() -> None:
    print(agent_hello(True).rstrip("\n"))
    print(
        encode_line(True, "playing", "Test Song", "Test Artist", "Music"),
        end="",
    )
    print()
    print(focus_encode(False), end="")
    print()
    modes = [{"id": "trabalho", "name": "Trabalho"}, {"id": "pessoal", "name": "Pessoal"}]
    print(
        focus_encode(True, "trabalho", "Trabalho", modes),
        end="",
    )
    print()


if __name__ == "__main__":
    main()
