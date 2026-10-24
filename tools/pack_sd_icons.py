#!/usr/bin/env python3
"""Pack Meteocons PNGs as WXIA files for /tessera on the microSD.

Copy the generated files onto the card (FAT32) as:
  /tessera/clear.wxia
  /tessera/partly.wxia
  ...
Or let the firmware seed them from flash on first successful mount.
"""
from __future__ import annotations

import struct
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "tools" / "weather_icons_src"
OUT = ROOT / "tmp" / "sd_tessera"
SIDE = 96

MAP = [
    ("clear-day.png", "clear.wxia"),
    ("partly-cloudy-day.png", "partly.wxia"),
    ("overcast.png", "overcast.wxia"),
    ("fog.png", "fog.wxia"),
    ("rain.png", "rain.wxia"),
    ("snow.png", "snow.wxia"),
    ("thunderstorms.png", "storm.wxia"),
    ("not-available.png", "empty.wxia"),
]


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for src_name, dst_name in MAP:
        im = Image.open(SRC / src_name).convert("RGBA")
        if im.size != (SIDE, SIDE):
            im = im.resize((SIDE, SIDE), Image.Resampling.LANCZOS)
        color = bytearray()
        alpha = bytearray()
        for r, g, b, a in im.getdata():
            c = rgb565(r, g, b)
            color.append(c & 0xFF)
            color.append((c >> 8) & 0xFF)
            alpha.append(a)
        payload = b"WXIA" + struct.pack("<HH", SIDE, SIDE) + bytes(color) + bytes(alpha)
        (OUT / dst_name).write_bytes(payload)
        print(f"{dst_name} {len(payload)}")
    print(f"ready: {OUT}")


if __name__ == "__main__":
    main()
