#!/usr/bin/env python3
"""Raster Simple Icons (CC0) brand SVGs → src/ui/aitokens_logos_data.cpp (RGB565A8)."""
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "tools" / "aitokens_logos_src"
OUT_C = ROOT / "src" / "ui" / "aitokens_logos_data.cpp"
OUT_H = ROOT / "src" / "ui" / "aitokens_logos_data.h"
SIDE = 24


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def pack(im: Image.Image) -> bytes:
    color = bytearray()
    alpha = bytearray()
    for r, g, b, a in im.convert("RGBA").getdata():
        c = rgb565(r, g, b)
        color += bytes((c & 0xFF, (c >> 8) & 0xFF))
        alpha.append(a)
    return bytes(color + alpha)


def tint_glyph(path: Path, rgb, size=16):
    im = Image.open(path).convert("RGBA")
    pixels = []
    for r, g, b, a in im.getdata():
        ink = 255 - (r + g + b) // 3
        ink = min(255, int(ink * (a / 255.0)))
        pixels.append((*rgb, ink))
    g = Image.new("RGBA", im.size)
    g.putdata(pixels)
    return g.resize((size, size), Image.Resampling.LANCZOS)


def badge(glyph, bg, radius_frac=0.28):
    canvas = Image.new("RGBA", (SIDE, SIDE), (0, 0, 0, 0))
    d = ImageDraw.Draw(canvas)
    d.rounded_rectangle((0, 0, SIDE - 1, SIDE - 1), radius=max(2, int(SIDE * radius_frac)), fill=(*bg, 255))
    gx, gy = glyph.size
    canvas.alpha_composite(glyph, ((SIDE - gx) // 2, (SIDE - gy) // 2))
    return canvas


def raster(svg: Path, png: Path):
    subprocess.check_call(
        ["rsvg-convert", "-w", "64", "-h", "64", "--background-color", "none", str(svg), "-o", str(png)]
    )


def main():
    raster(SRC / "openai.svg", SRC / "openai-raw.png")
    raster(SRC / "anthropic.svg", SRC / "anthropic-raw.png")
    raster(SRC / "cursor.svg", SRC / "cursor-raw.png")

    codex = badge(tint_glyph(SRC / "openai-raw.png", (255, 255, 255), 15), (16, 16, 16), 0.5)
    claude_g = tint_glyph(SRC / "anthropic-raw.png", (217, 119, 87), 20)
    claude = Image.new("RGBA", (SIDE, SIDE), (0, 0, 0, 0))
    claude.alpha_composite(claude_g, ((SIDE - claude_g.size[0]) // 2, (SIDE - claude_g.size[1]) // 2))
    cursor = badge(tint_glyph(SRC / "cursor-raw.png", (255, 255, 255), 15), (0, 0, 0), 0.28)

    icons = [("codex", codex), ("claude", claude), ("cursor", cursor)]
    for name, im in icons:
        im.save(SRC / f"{name}.png")

    parts = [(n, pack(im)) for n, im in icons]
    lines = [
        "// Generated from Simple Icons (CC0) official brand paths. Do not edit.",
        '#include "ui/aitokens_logos_data.h"',
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
    lines.append("namespace aitokens_logos_data {")
    lines.append("int side() { return kSide; }")
    for cname, _ in parts:
        lines.append(
            f"const uint8_t *{cname}(size_t *bytes) {{ if (bytes) *bytes = kBytes; return {cname}_data; }}"
        )
    lines.append("}  // namespace aitokens_logos_data")
    lines.append("")
    OUT_C.write_text("\n".join(lines))
    OUT_H.write_text(
        """#pragma once

#include <stddef.h>
#include <stdint.h>

// Simple Icons (CC0) product marks, RGB565A8.
namespace aitokens_logos_data {
int side();
const uint8_t *codex(size_t *bytes);
const uint8_t *claude(size_t *bytes);
const uint8_t *cursor(size_t *bytes);
}
"""
    )
    print("wrote", OUT_C)


if __name__ == "__main__":
    main()
