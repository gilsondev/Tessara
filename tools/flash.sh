#!/bin/sh
# Build, reboot the board into the ROM bootloader over USB and flash it.
set -e
cd "$(dirname "$0")/.."
.venv/bin/pio run -e jc3248w535
.venv/bin/python - <<'PY'
import glob, time, serial
ports = sorted(p for p in glob.glob("/dev/cu.usbmodem*") if not p.endswith("1401"))
if ports:
    try:
        s = serial.Serial(ports[0], 115200, timeout=0.2)
        s.write(b"FLASH\n")
        s.flush()
        deadline = time.time() + 1.5
        while time.time() < deadline and b"flash pronto" not in s.read(256):
            pass
        s.close()
    except (serial.SerialException, OSError):
        pass
    try:
        s = serial.Serial(ports[0], 1200)
        s.dtr = False
        s.rts = False
        time.sleep(0.4)
        s.close()
    except (serial.SerialException, OSError):
        pass
for _ in range(60):
    if glob.glob("/dev/cu.usbmodem1401"):
        break
    time.sleep(0.15)
else:
    raise SystemExit("bootloader nao apareceu")
PY
.venv/bin/pio run -t upload --upload-port /dev/cu.usbmodem1401
