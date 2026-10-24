#!/usr/bin/env python3
"""Raster Material Icons Wi-Fi SVGs (Apache 2.0) → wifi_icons_data.cpp (RGB565A8)."""
from pathlib import Path
import subprocess

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "tools" / "wifi_icons_src"
OUT_C = ROOT / "firmware" / "components" / "ui_kit" / "ui" / "wifi_icons_data.cpp"
OUT_H = ROOT / "firmware" / "components" / "ui_kit" / "ui" / "wifi_icons_data.h"
SIDE = 20

# Material Icons baseline — Google, Apache-2.0.
ICONS = [
    ("strong", "wifi"),
    ("medium", "wifi_2_bar"),
    ("weak", "wifi_1_bar"),
    ("off", "signal_wifi_off"),
]


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def pack(im: Image.Image) -> bytes:
    color = bytearray()
    alpha = bytearray()
    for r, g, b, a in im.getdata():
        c = rgb565(r, g, b)
        color += bytes((c & 0xFF, (c >> 8) & 0xFF))
        alpha.append(a)
    return bytes(color + alpha)


def to_white_mask(im: Image.Image) -> Image.Image:
    """Glifo preto → branco com alpha (recolor LVGL)."""
    out = []
    for r, g, b, a in im.convert("RGBA").getdata():
        luma = (r + g + b) // 3
        ink = int(a * (255 - luma) / 255)
        out.append((255, 255, 255, ink))
    g = Image.new("RGBA", im.size)
    g.putdata(out)
    return g.resize((SIDE, SIDE), Image.Resampling.LANCZOS)


def raster(svg: Path, png: Path):
    subprocess.check_call(
        ["rsvg-convert", "-w", "64", "-h", "64", "--background-color", "none", str(svg), "-o", str(png)]
    )


def main():
    parts = []
    for cname, stem in ICONS:
        svg = SRC / f"{stem}.svg"
        raw = SRC / f"{stem}-raw.png"
        raster(svg, raw)
        im = to_white_mask(Image.open(raw))
        im.save(SRC / f"{cname}.png")
        parts.append((cname, pack(im)))

    lines = [
        "// Generated from Material Icons (Apache-2.0). Do not edit.",
        '// Source: tools/wifi_icons_src/*.svg via tools/gen_wifi_icons.py',
        '#include "ui/wifi_icons_data.h"',
        "",
        f"static constexpr int kSide = {SIDE};",
        f"static constexpr size_t kPix = {SIDE * SIDE};",
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
    lines.append("namespace wifi_icons_data {")
    lines.append("int side() { return kSide; }")
    for cname, _ in parts:
        lines.append(
            f"const uint8_t *{cname}(size_t *bytes) {{ if (bytes) *bytes = kBytes; return {cname}_data; }}"
        )
    lines.append("}  // namespace wifi_icons_data")
    lines.append("")
    OUT_C.write_text("\n".join(lines))
    OUT_H.write_text(
        """#pragma once

#include <stddef.h>
#include <stdint.h>

// Material Icons Wi-Fi (Apache-2.0), RGB565A8 white glyph + alpha.
namespace wifi_icons_data {

int side();
const uint8_t *strong(size_t *bytes = nullptr);
const uint8_t *medium(size_t *bytes = nullptr);
const uint8_t *weak(size_t *bytes = nullptr);
const uint8_t *off(size_t *bytes = nullptr);

}  // namespace wifi_icons_data
"""
    )
    print(f"wrote {OUT_C} ({OUT_C.stat().st_size} bytes), side={SIDE}")


if __name__ == "__main__":
    main()
