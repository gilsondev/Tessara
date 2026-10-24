#!/usr/bin/env python3
"""Raster Meteocons fill SVGs → src/ui/weather_icons_data.cpp (RGB565A8)."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "tools" / "weather_icons_src"
OUT_C = ROOT / "src" / "ui" / "weather_icons_data.cpp"
OUT_H = ROOT / "src" / "ui" / "weather_icons_data.h"

ICONS = [
    ("clear_day", "clear-day"),
    ("partly_cloudy", "partly-cloudy-day"),
    ("overcast", "overcast"),
    ("fog", "fog"),
    ("rain", "rain"),
    ("snow", "snow"),
    ("thunderstorms", "thunderstorms"),
    ("unavailable", "not-available"),
]


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def main():
    side = None
    parts = []
    for cname, stem in ICONS:
        im = Image.open(SRC / f"{stem}.png").convert("RGBA")
        w, h = im.size
        assert w == h, (stem, w, h)
        side = w
        color = bytearray()
        alpha = bytearray()
        for r, g, b, a in im.getdata():
            c = rgb565(r, g, b)
            color.append(c & 0xFF)
            color.append((c >> 8) & 0xFF)
            alpha.append(a)
        parts.append((cname, bytes(color + alpha)))

    lines = [
        "// Generated from Meteocons MIT (@meteocons/svg-static fill). Do not edit.",
        '#include "ui/weather_icons_data.h"',
        "",
        f"static constexpr int kSide = {side};",
        f"static constexpr size_t kPix = {side * side};",
        "static constexpr size_t kBytes = kPix * 3;",
        "",
    ]
    for cname, packed in parts:
        lines.append(f"static const uint8_t {cname}_data[kBytes] = {{")
        row = []
        for b in packed:
            row.append(f"0x{b:02X}")
            if len(row) == 16:
                lines.append("    " + ", ".join(row) + ",")
                row = []
        if row:
            lines.append("    " + ", ".join(row) + ",")
        lines.append("};")
        lines.append("")
    lines.append("namespace weather_icons_data {")
    lines.append("int side() { return kSide; }")
    for cname, _ in parts:
        lines.append(
            f"const uint8_t *{cname}(size_t *bytes) {{ if (bytes) *bytes = kBytes; return {cname}_data; }}"
        )
    lines.append("}  // namespace weather_icons_data")
    lines.append("")
    OUT_C.write_text("\n".join(lines))
    OUT_H.write_text(
        """#pragma once

#include <stddef.h>
#include <stdint.h>

// Meteocons fill (MIT), RGB565A8 (color plane then alpha) for LVGL.
namespace weather_icons_data {

int side();
const uint8_t *clear_day(size_t *bytes = nullptr);
const uint8_t *partly_cloudy(size_t *bytes = nullptr);
const uint8_t *overcast(size_t *bytes = nullptr);
const uint8_t *fog(size_t *bytes = nullptr);
const uint8_t *rain(size_t *bytes = nullptr);
const uint8_t *snow(size_t *bytes = nullptr);
const uint8_t *thunderstorms(size_t *bytes = nullptr);
const uint8_t *unavailable(size_t *bytes = nullptr);

}  // namespace weather_icons_data
"""
    )
    print(f"wrote {OUT_C} ({OUT_C.stat().st_size} bytes), side={side}")


if __name__ == "__main__":
    main()
