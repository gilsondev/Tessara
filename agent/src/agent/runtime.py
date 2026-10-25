"""Loop USB: handshake AGENT + capabilities (AI Tokens, Calendar)."""

from __future__ import annotations

import glob
import time

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit("instale pyserial: pip install pyserial") from exc

from agent.capabilities import aitokens, calendar, media
from agent.protocol import agent_hello
from agent.self_update import maybe_self_update

PUSH_S = 5.0
CALENDAR_PUSH_S = 20.0  # keep agenda warm; USB is lossy on reconnect
MEDIA_PUSH_S = 8.0  # heartbeat; on-change also answered via MEDIA?
RETRY_S = 1.5
BAUD = 115200


def app_ports():
    return sorted(p for p in glob.glob("/dev/cu.usbmodem*") if not p.endswith("1401"))


def open_serial():
    for port in app_ports():
        try:
            # dsrdtr=False avoids the macOS CDC reset pulse on open.
            ser = serial.Serial(
                port=port,
                baudrate=BAUD,
                timeout=0.2,
                write_timeout=1.0,
                # dsrdtr=False avoids the macOS CDC reset pulse on open.
                dsrdtr=False,
                rtscts=False,
            )
            try:
                ser.rts = False
            except Exception:
                pass
            time.sleep(0.5)  # settle after open (CDC enumerate)
            # ESP32 USB CDC suppresses device→host TX while DTR is low, so panel
            # commands (MEDIA\tPLAYPAUSE, CALENDAR?, …) never arrive. Assert DTR
            # after settle — open already happened without the reset pulse.
            try:
                ser.dtr = True
            except Exception:
                pass
            return ser, port
        except (OSError, serial.SerialException):
            continue
    return None, None


def write_lines(ser, lines: list[str]) -> None:
    for line in lines:
        if not line:
            continue
        ser.write((line.rstrip("\n") + "\n").encode())
        ser.flush()
        time.sleep(0.02)  # let the panel finish the line before the next


def main() -> int:
    ser = None
    port = None
    last_push = 0.0
    last_cal = 0.0
    last_media = 0.0
    last_update_check = [0.0]
    buf = bytearray()
    print("tessara-agent: aguardando painel USB…", flush=True)
    while True:
        if ser is None or not ser.is_open:
            ser, port = open_serial()
            if ser is None:
                time.sleep(RETRY_S)
                continue
            print(f"tessara-agent: conectado em {port}", flush=True)
            try:
                ser.write(agent_hello(True).encode())
                ser.flush()
            except (OSError, serial.SerialException):
                pass
            last_push = 0.0
            last_cal = 0.0
            last_media = 0.0
            buf.clear()
            # Fresh USB link: check panel package once (then every CHECK_S).
            if maybe_self_update(force_check=True, last_check=last_update_check):
                return 0
        try:
            chunk = ser.read(256)
            if chunk:
                buf.extend(chunk)
                while b"\n" in buf:
                    line, _, buf = buf.partition(b"\n")
                    text = line.decode("utf-8", "replace").strip()
                    if text.startswith("AITOKENS?"):
                        write_lines(ser, [aitokens.build_line()])
                        last_push = time.time()
                    elif text.startswith("MEDIA"):
                        if "\t" in text and not text.endswith("?"):
                            print(f"tessara-agent: media ← {text}", flush=True)
                        replies = media.handle_line(text)
                        if replies:
                            write_lines(ser, replies)
                            last_media = time.time()
                    elif text.startswith("CALENDAR"):
                        replies = calendar.handle_line(text)
                        if replies:
                            write_lines(ser, replies)
                            last_cal = time.time()
                        elif text.startswith("CALENDAR?"):
                            print("tessara-agent: calendar ← ?", flush=True)
            now = time.time()
            if maybe_self_update(last_check=last_update_check):
                return 0
            # Keep capability pushes on separate turns so a partial USB read
            # cannot glue AITOKENS fields into a CALENDAR_CATALOG line.
            if now - last_cal >= CALENDAR_PUSH_S:
                write_lines(ser, calendar.push_lines())
                last_cal = now
            elif now - last_media >= MEDIA_PUSH_S:
                write_lines(ser, [media.build_line()])
                last_media = now
            elif now - last_push >= PUSH_S:
                ser.write(agent_hello(True).encode())
                ser.write((aitokens.build_line() + "\n").encode())
                ser.flush()
                last_push = now
        except (OSError, serial.SerialException) as exc:
            print(f"tessara-agent: porta liberada ({exc})", flush=True)
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(RETRY_S * 2)  # back off — tight loops worsen CDC resets
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
