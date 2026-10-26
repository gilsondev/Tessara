#!/usr/bin/env python3
"""Render Logo Kit v1 masters → agent-ui icons + firmware RGB565A8 glyphs.

Source of truth: brand/symbol-*.svg and brand/lockup-*.svg (transparent).
App icon uses brand/mark.svg (slate plate). Tray uses white mono symbol.
"""
from __future__ import annotations

import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BRAND = ROOT / "brand"
ICONS = ROOT / "apps" / "agent-ui" / "src-tauri" / "icons"
ASSETS = ROOT / "apps" / "agent-ui" / "src" / "assets"
FW_UI = ROOT / "firmware" / "components" / "dashboard_shell" / "ui"

MARK = BRAND / "mark.svg"  # Dock / app icon (opaque plate)
MARK_CLEAR = BRAND / "symbol-light.svg"  # colored, transparent
MARK_TRAY = BRAND / "mark-tray.svg"  # white mono
LOCKUP_LIGHT = BRAND / "lockup-light.svg"
LOCKUP_DARK = BRAND / "lockup-dark.svg"

# Home draws lockup at 108×30 — generate at native display size.
LOCKUP_W = 108
LOCKUP_H = 30

PNG_SIZES = {
    "32x32.png": 32,
    "128x128.png": 128,
    "128x128@2x.png": 256,
    "icon.png": 512,
    "StoreLogo.png": 50,
    "Square30x30Logo.png": 30,
    "Square44x44Logo.png": 44,
    "Square71x71Logo.png": 71,
    "Square89x89Logo.png": 89,
    "Square107x107Logo.png": 107,
    "Square142x142Logo.png": 142,
    "Square150x150Logo.png": 150,
    "Square284x284Logo.png": 284,
    "Square310x310Logo.png": 310,
}

TRAY_SIZES = {
    "trayTemplate.png": 18,
    "trayTemplate@2x.png": 36,
}


def which(name: str) -> str:
    path = shutil.which(name)
    if not path:
        raise SystemExit(f"missing tool: {name}")
    return path


def render(svg: Path, out: Path, side: int, background: str | None = None) -> None:
    cmd = [which("rsvg-convert"), "-w", str(side), "-h", str(side)]
    if background:
        cmd += ["--background-color", background]
    cmd += [str(svg), "-o", str(out)]
    subprocess.check_call(cmd)


def render_wh(svg: Path, out: Path, w: int, h: int) -> None:
    """Render preserving transparency (no background fill)."""
    subprocess.check_call(
        [which("rsvg-convert"), "-w", str(w), "-h", str(h), str(svg), "-o", str(out)]
    )


def build_icns(dest: Path) -> None:
    """iconutil iconset from a 1024 master (macOS)."""
    with tempfile.TemporaryDirectory() as tmp:
        iconset = Path(tmp) / "AppIcon.iconset"
        iconset.mkdir()
        master = Path(tmp) / "master.png"
        render(MARK, master, 1024)
        sizes = [
            ("icon_16x16.png", 16),
            ("icon_16x16@2x.png", 32),
            ("icon_32x32.png", 32),
            ("icon_32x32@2x.png", 64),
            ("icon_128x128.png", 128),
            ("icon_128x128@2x.png", 256),
            ("icon_256x256.png", 256),
            ("icon_256x256@2x.png", 512),
            ("icon_512x512.png", 512),
            ("icon_512x512@2x.png", 1024),
        ]
        for name, side in sizes:
            subprocess.check_call(
                [
                    which("sips"),
                    "-z",
                    str(side),
                    str(side),
                    str(master),
                    "--out",
                    str(iconset / name),
                ],
                stdout=subprocess.DEVNULL,
            )
        subprocess.check_call([which("iconutil"), "-c", "icns", str(iconset), "-o", str(dest)])


def build_ico(src_pngs: list[Path], dest: Path) -> None:
    entries = []
    for png in src_pngs:
        data = png.read_bytes()
        assert data[:8] == b"\x89PNG\r\n\x1a\n"
        w, h = struct.unpack(">II", data[16:24])
        if w > 256 or h > 256:
            continue
        entries.append((w if w < 256 else 0, h if h < 256 else 0, data))
    if not entries:
        raise SystemExit("no ICO frames")
    header = struct.pack("<HHH", 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    directory = b""
    payloads = b""
    for w, h, data in entries:
        directory += struct.pack("<BBBBHHII", w, h, 0, 0, 1, 32, len(data), offset)
        payloads += data
        offset += len(data)
    dest.write_bytes(header + directory + payloads)


def rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def pack_rgb565a8(png: Path) -> tuple[bytes, int, int]:
    """RGB565 color plane + A8 alpha plane (LVGL RGB565A8). Returns (packed, w, h)."""
    data = png.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos = 8
    w = h = 0
    bit_depth = color = 0
    raw = b""
    plte = b""
    trns = b""
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        ctype = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            w, h, bit_depth, color, *_ = struct.unpack(">IIBBBBB", chunk)
        elif ctype == b"PLTE":
            plte = chunk
        elif ctype == b"tRNS":
            trns = chunk
        elif ctype == b"IDAT":
            raw += chunk
        elif ctype == b"IEND":
            break
    import zlib

    decompressed = zlib.decompress(raw)
    stride = {0: (w * bit_depth + 7) // 8, 2: w * 3, 3: w, 4: w * 2, 6: w * 4}[color]
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    rows = []
    i = 0
    prev = bytearray(stride)
    for _y in range(h):
        filt = decompressed[i]
        i += 1
        row = bytearray(decompressed[i : i + stride])
        i += stride
        if bit_depth != 8 and color != 3:
            raise SystemExit(f"unsupported bit depth {bit_depth}")
        out = bytearray(stride)
        for x in range(stride):
            left = out[x - bpp] if x >= bpp else 0
            up = prev[x]
            up_left = prev[x - bpp] if x >= bpp else 0
            val = row[x]
            if filt == 0:
                out[x] = val
            elif filt == 1:
                out[x] = (val + left) & 255
            elif filt == 2:
                out[x] = (val + up) & 255
            elif filt == 3:
                out[x] = (val + ((left + up) // 2)) & 255
            elif filt == 4:
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                pr = left if pa <= pb and pa <= pc else (up if pb <= pc else up_left)
                out[x] = (val + pr) & 255
            else:
                raise SystemExit(f"bad png filter {filt}")
        rows.append(bytes(out))
        prev = out

    color_b = bytearray()
    alpha_b = bytearray()
    for row in rows:
        for x in range(w):
            if color == 6:
                r, g, b, a = row[x * 4 : x * 4 + 4]
            elif color == 2:
                r, g, b = row[x * 3 : x * 3 + 3]
                a = 255
            elif color == 3:
                idx = row[x]
                r, g, b = plte[idx * 3 : idx * 3 + 3]
                a = trns[idx] if trns and idx < len(trns) else 255
            else:
                raise SystemExit(f"unsupported color type {color}")
            c = rgb565(r, g, b)
            color_b += bytes((c & 0xFF, (c >> 8) & 0xFF))
            alpha_b.append(a)
    return bytes(color_b + alpha_b), w, h


def emit_c_array(name: str, data: bytes) -> list[str]:
    lines = [f"static const uint8_t {name}[] = {{"]
    row: list[str] = []
    for b in data:
        row.append(f"0x{b:02X}")
        if len(row) == 16:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines.append("};")
    return lines


def write_firmware_mark(side: int = 24) -> None:
    """Small RGB565A8 mark for settings / Agente rows (transparent)."""
    with tempfile.TemporaryDirectory() as tmp:
        png = Path(tmp) / "mark.png"
        render_wh(MARK_CLEAR, png, side, side)
        packed, w, h = pack_rgb565a8(png)
        assert (w, h) == (side, side)
        out_h = FW_UI / "tessera_mark_icon.h"
        out_c = FW_UI / "tessera_mark_icon.cpp"
        out_h.write_text(
            "// Generated by tools/gen_agent_icons.py — do not edit.\n"
            "#pragma once\n"
            "#include <lvgl.h>\n"
            "\n"
            "// 24×24 Tessera symbol (Logo Kit v1, transparent) for settings / Agente rows.\n"
            "const lv_image_dsc_t &tessera_mark_icon();\n"
        )
        lines = [
            "// Generated by tools/gen_agent_icons.py from brand/symbol-light.svg — do not edit.",
            '#include "ui/tessera_mark_icon.h"',
            "",
            f"static constexpr int kSide = {side};",
            f"static constexpr size_t kBytes = {side * side * 3};",
            "",
        ]
        lines += emit_c_array("kMark", packed)
        lines += [
            "",
            "const lv_image_dsc_t &tessera_mark_icon() {",
            "    static lv_image_dsc_t image = {};",
            "    static bool ready = false;",
            "    if (!ready) {",
            "        image.header.magic = LV_IMAGE_HEADER_MAGIC;",
            "        image.header.cf = LV_COLOR_FORMAT_RGB565A8;",
            "        image.header.flags = 0;",
            "        image.header.w = kSide;",
            "        image.header.h = kSide;",
            "        image.header.stride = kSide * 2;",
            "        image.data_size = kBytes;",
            "        image.data = kMark;",
            "        ready = true;",
            "    }",
            "    return image;",
            "}",
            "",
        ]
        out_c.write_text("\n".join(lines))
        print(f"firmware mark {side}px → {out_c.relative_to(ROOT)}")


def write_firmware_lockup() -> None:
    """Home lockup light/dark → brand_mark.cpp as RGB565A8 (transparent)."""
    if not LOCKUP_LIGHT.exists() or not LOCKUP_DARK.exists():
        raise SystemExit("missing brand/lockup-light.svg or lockup-dark.svg")
    with tempfile.TemporaryDirectory() as tmp:
        light_png = Path(tmp) / "light.png"
        dark_png = Path(tmp) / "dark.png"
        render_wh(LOCKUP_LIGHT, light_png, LOCKUP_W, LOCKUP_H)
        render_wh(LOCKUP_DARK, dark_png, LOCKUP_W, LOCKUP_H)
        light, w, h = pack_rgb565a8(light_png)
        dark, w2, h2 = pack_rgb565a8(dark_png)
        assert (w, h) == (LOCKUP_W, LOCKUP_H) and (w2, h2) == (LOCKUP_W, LOCKUP_H)

        out = FW_UI / "brand_mark.cpp"
        parts = [
            "// Generated by tools/gen_agent_icons.py from brand/lockup-*.svg (Logo Kit v1) — do not edit.",
            '#include "ui/brand_mark.h"',
            "",
        ]
        parts += emit_c_array("kLight", light)
        parts.append("")
        parts += emit_c_array("kDark", dark)
        parts += [
            "",
            "namespace {",
            "lv_image_dsc_t make(const uint8_t *pixels, uint32_t bytes) {",
            "    lv_image_dsc_t image = {};",
            "    image.header.magic = LV_IMAGE_HEADER_MAGIC;",
            "    image.header.cf = LV_COLOR_FORMAT_RGB565A8;",
            f"    image.header.w = {LOCKUP_W};",
            f"    image.header.h = {LOCKUP_H};",
            f"    image.header.stride = {LOCKUP_W} * 2;",
            "    image.data_size = bytes;",
            "    image.data = pixels;",
            "    return image;",
            "}",
            "}  // namespace",
            "",
            "const lv_image_dsc_t &tessera_lockup(bool dark) {",
            "    static const lv_image_dsc_t light = make(kLight, sizeof(kLight));",
            "    static const lv_image_dsc_t night = make(kDark, sizeof(kDark));",
            "    return dark ? night : light;",
            "}",
            "",
        ]
        out.write_text("\n".join(parts))
        print(f"firmware lockup {LOCKUP_W}x{LOCKUP_H} RGB565A8 → {out.relative_to(ROOT)}")


def main() -> None:
    required = (MARK, MARK_CLEAR, MARK_TRAY, LOCKUP_LIGHT, LOCKUP_DARK)
    for p in required:
        if not p.exists():
            raise SystemExit(f"missing {p}")

    ICONS.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)

    for name, side in PNG_SIZES.items():
        render(MARK, ICONS / name, side)
        print("icon", name)

    render_wh(MARK_CLEAR, ASSETS / "tessera-mark.png", 128, 128)
    print("asset tessera-mark.png")

    for name, side in TRAY_SIZES.items():
        render(MARK_TRAY, ICONS / name, side)
        print("tray", name)

    build_icns(ICONS / "icon.icns")
    print("icon.icns")
    ico_frames = []
    with tempfile.TemporaryDirectory() as tmp:
        for side in (16, 32, 48, 64, 128, 256):
            p = Path(tmp) / f"{side}.png"
            render(MARK, p, side)
            ico_frames.append(p)
        build_ico(ico_frames, ICONS / "icon.ico")
    print("icon.ico")

    write_firmware_mark(24)
    write_firmware_lockup()
    print("done")


if __name__ == "__main__":
    main()
