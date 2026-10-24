#!/usr/bin/env python3
"""Tessara AI Tokens Bridge: push Codex/Claude snapshots over USB serial."""

from __future__ import annotations

import glob
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import claude_source
import codex_source
import cursor_source

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit("instale pyserial: pip install pyserial") from exc

PUSH_S = 5.0
RETRY_S = 1.5
BAUD = 115200


def app_ports():
    return sorted(p for p in glob.glob("/dev/cu.usbmodem*") if not p.endswith("1401"))


def encode_source(src: dict) -> str:
    return "\t".join(
        [
            src["id"],
            "1" if src.get("available") else "0",
            src.get("kind") or "none",
            str(int(src.get("value") or 0)),
            str(src.get("display") or "—").replace("\t", " "),
            str(src.get("reset") or "").replace("\t", " "),
        ]
    )


def build_line() -> str:
    sources = [codex_source.read(), claude_source.read(), cursor_source.read()]
    ok = any(s.get("available") for s in sources)
    body = "\t".join(encode_source(s) for s in sources)
    return "AITOKENS\tv1\t" + ("1" if ok else "0") + "\t" + body


def open_serial():
    for port in app_ports():
        try:
            ser = serial.Serial()
            ser.port = port
            ser.baudrate = BAUD
            ser.timeout = 0.2
            ser.write_timeout = 1.0
            ser.dtr = True
            ser.rts = False
            ser.open()
            return ser, port
        except (OSError, serial.SerialException):
            continue
    return None, None


def main() -> int:
    ser = None
    port = None
    last_push = 0.0
    buf = bytearray()
    print("aitokens-bridge: aguardando painel USB…", flush=True)
    while True:
        if ser is None or not ser.is_open:
            ser, port = open_serial()
            if ser is None:
                time.sleep(RETRY_S)
                continue
            print(f"aitokens-bridge: conectado em {port}", flush=True)
            last_push = 0.0
            buf.clear()
        try:
            chunk = ser.read(256)
            if chunk:
                buf.extend(chunk)
                while b"\n" in buf:
                    line, _, buf = buf.partition(b"\n")
                    text = line.decode("utf-8", "replace").strip()
                    if text.startswith("AITOKENS?"):
                        ser.write((build_line() + "\n").encode())
                        ser.flush()
                        last_push = time.time()
            now = time.time()
            if now - last_push >= PUSH_S:
                ser.write((build_line() + "\n").encode())
                ser.flush()
                last_push = now
        except (OSError, serial.SerialException) as exc:
            print(f"aitokens-bridge: porta liberada ({exc})", flush=True)
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(RETRY_S)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
