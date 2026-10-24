"""Loop USB: handshake AGENT + capability AI Tokens."""

from __future__ import annotations

import glob
import sys
import time

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit("instale pyserial: pip install pyserial") from exc

from agent.capabilities import aitokens
from agent.protocol import agent_hello

PUSH_S = 5.0
RETRY_S = 1.5
BAUD = 115200


def app_ports():
    return sorted(p for p in glob.glob("/dev/cu.usbmodem*") if not p.endswith("1401"))


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
    print("tessera-agent: aguardando painel USB…", flush=True)
    while True:
        if ser is None or not ser.is_open:
            ser, port = open_serial()
            if ser is None:
                time.sleep(RETRY_S)
                continue
            print(f"tessera-agent: conectado em {port}", flush=True)
            try:
                # Agente no ar; cotas vêm em AITOKENS (pode estar vazio).
                ser.write(agent_hello(True).encode())
                ser.flush()
            except (OSError, serial.SerialException):
                pass
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
                        ser.write((aitokens.build_line() + "\n").encode())
                        ser.flush()
                        last_push = time.time()
            now = time.time()
            if now - last_push >= PUSH_S:
                # AGENT periódico: o painel marca stale se o handshake parar.
                ser.write(agent_hello(True).encode())
                ser.write((aitokens.build_line() + "\n").encode())
                ser.flush()
                last_push = now
        except (OSError, serial.SerialException) as exc:
            print(f"tessera-agent: porta liberada ({exc})", flush=True)
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(RETRY_S)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
