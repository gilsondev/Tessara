#!/usr/bin/env python3
"""Drive drags on the panel over USB and read back PERF after each one.

Needs the serial port free, so stop agentd first:
    launchctl bootout gui/$(id -u)/com.tessera.agentd

  --snap PATH   save a screenshot (PNG) and exit
  --tap X Y     tap once before the drags (to open a tile)
  --seek N      swipe left N times before measuring (reach a longer list)
  --no-drags    only navigate, do not measure
Port comes from $TESSERA_PORT.
"""
import argparse
import os
import sys
import time
import zlib

import serial

PORT = os.environ.get("TESSERA_PORT", "/dev/cu.usbmodem288485548A341")
DRAGS = [
    ("horizontal <-", "250 260 60 260 220"),
    ("horizontal ->", "60 260 250 260 220"),
    ("vertical up", "160 380 160 120 220"),
    ("vertical down", "160 120 160 380 220"),
]


def png(path: str, width: int, height: int, rgb: bytes) -> None:
    raw = b"".join(b"\x00" + rgb[y * width * 3 : (y + 1) * width * 3] for y in range(height))

    def chunk(tag: bytes, body: bytes) -> bytes:
        return (
            len(body).to_bytes(4, "big")
            + tag
            + body
            + (zlib.crc32(tag + body) & 0xFFFFFFFF).to_bytes(4, "big")
        )

    header = width.to_bytes(4, "big") + height.to_bytes(4, "big") + b"\x08\x02\x00\x00\x00"
    with open(path, "wb") as out:
        out.write(b"\x89PNG\r\n\x1a\n")
        out.write(chunk(b"IHDR", header))
        out.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        out.write(chunk(b"IEND", b""))


def snapshot(port: serial.Serial, path: str) -> None:
    port.reset_input_buffer()
    port.write(b"SNAP\n")
    port.flush()
    while True:
        line = port.readline()
        if not line:
            raise SystemExit("sem resposta de SNAP")
        if line.startswith(b"SNAP "):
            _, w, h = line.split()
            width, height = int(w), int(h)
            break
    need = width * height * 3
    data = b""
    while len(data) < need:
        piece = port.read(need - len(data))
        if not piece:
            raise SystemExit(f"SNAP truncado: {len(data)}/{need}")
        data += piece
    png(path, width, height, data)
    print(f"{path} ({width}x{height})")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--snap")
    parser.add_argument("--tap", nargs=2, type=int)
    parser.add_argument("--no-drags", action="store_true")
    parser.add_argument("--seek", type=int, default=0, help="swipes left before measuring")
    args = parser.parse_args()

    port = serial.Serial(PORT, 115200, timeout=0.5)
    time.sleep(0.8)
    port.reset_input_buffer()

    def send(text: str) -> None:
        port.write((text + "\n").encode())
        port.flush()

    def drain(seconds: float) -> list[str]:
        lines = []
        end = time.time() + seconds
        while time.time() < end:
            raw = port.readline()
            if raw:
                lines.append(raw.decode("utf8", "replace").rstrip())
        return lines

    if args.tap:
        x, y = args.tap
        send(f"DRAG {x} {y} {x} {y} 80")
        time.sleep(2.0)
        drain(0.5)

    for _ in range(args.seek):
        send("DRAG 250 260 60 260 220")
        time.sleep(1.6)
    if args.seek:
        time.sleep(2.5)  # let idle_step warm the scroll cache and previews
        drain(0.5)

    if args.snap:
        snapshot(port, args.snap)
        port.close()
        return 0

    if not args.no_drags:
        send("PERF")
        drain(1.5)  # reset counters
        for label, drag in DRAGS:
            send(f"DRAG {drag}")
            time.sleep(1.2)
            send("PERF")
            print(f"--- {label} ---")
            print("\n".join(l for l in drain(1.5) if l.startswith(("PERF", "UI", "BENCH", "SLIDE"))))
            time.sleep(1.5)

    port.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
