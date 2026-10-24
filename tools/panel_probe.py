"""Drive the panel over USB: wait for idle, run scripted drags, read PERF and a framebuffer snapshot."""

import glob
import pathlib
import subprocess
import sys
import time

import serial


def app_port():
    ports = sorted(p for p in glob.glob("/dev/cu.usbmodem*") if not p.endswith("1401"))
    return ports[0] if ports else None


def open_port(timeout=25):
    deadline = time.time() + timeout
    while time.time() < deadline:
        port = app_port()
        if port:
            try:
                ser = serial.Serial()
                ser.port = port
                ser.baudrate = 115200
                ser.timeout = 0.2
                ser.dtr = True
                ser.rts = False
                ser.open()
                return ser
            except OSError:
                pass
        time.sleep(0.3)
    raise SystemExit("porta do painel ausente")


def read_until_quiet(ser, quiet=2.5, limit=60):
    log = bytearray()
    last = time.time()
    end = time.time() + limit
    while time.time() < end:
        chunk = ser.read(4096)
        if chunk:
            log += chunk
            last = time.time()
        elif time.time() - last > quiet:
            break
    return log.decode("utf-8", "replace")


def command(ser, line, wait=0.6):
    ser.write((line + "\n").encode())
    ser.flush()
    time.sleep(wait)
    return ser.read(65536).decode("utf-8", "replace")


def perf_during(ser, label, drags, settle=1.2):
    command(ser, "PERF", 0.3)
    for drag in drags:
        command(ser, "DRAG " + drag, 0.05)
        ms = int(drag.split()[-1])
        time.sleep(ms / 1000 + 0.12)
    time.sleep(settle)
    out = command(ser, "PERF", 0.5)
    lines = [l for l in out.splitlines() if l.startswith(("PERF", "UI ", "TOUCH"))]
    print(f"{label}: {' | '.join(lines) if lines else out.strip()}")


def snapshot(ser, path):
    ser.reset_input_buffer()
    ser.write(b"SNAP\n")
    ser.flush()
    raw = bytearray()
    end = time.time() + 25
    header = None
    while time.time() < end and header is None:
        raw += ser.read(32768)
        data = bytes(raw)
        idx = data.find(b"SNAP ")
        if idx >= 0:
            nl = data.find(b"\n", idx)
            if nl >= 0:
                header = data[idx:nl].decode()
                raw = bytearray(data[nl + 1:])
    if not header:
        print("sem snapshot")
        return
    w, h = (int(v) for v in header.split()[1:3])
    need = w * h * 3
    while len(raw) < need and time.time() < end:
        raw += ser.read(65536)
    ppm = pathlib.Path(path).with_suffix(".ppm")
    ppm.write_bytes(f"P6\n{w} {h}\n255\n".encode() + bytes(raw[:need]))
    subprocess.run(["sips", "-s", "format", "png", str(ppm), "--out", path], capture_output=True)
    print("snapshot", path)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "all"
    ser = open_port()
    if mode == "boot":
        snapshot(ser, "/tmp/todoist-panel-boot.png")
    if mode == "refresh":
        command(ser, "RELOAD", 1.0)
        snapshot(ser, "/tmp/todoist-panel-refresh.png")
    log = read_until_quiet(ser, quiet=3.0, limit=70)
    for line in log.splitlines():
        if line.startswith(("GLYPH", "DUE", "wi-fi", "tela", "display", "todoist /api/v1/sync", "todoist-panel")):
            print("log:", line)
    if mode in ("all", "perf"):
        perf_during(ser, "parado", [])
        perf_during(ser, "rolagem vertical", ["240 290 240 110 450", "240 110 240 290 450"])
        perf_during(ser, "slide para a proxima lista", ["400 220 80 220 350"], settle=1.5)
        perf_during(ser, "slide de volta", ["80 220 400 220 350"], settle=1.5)
        perf_during(ser, "rolagem dos filtros", ["420 80 60 80 500", "60 80 420 80 500"])
        perf_during(ser, "abrir Trabalho", ["286 76 286 76 60"], settle=1.5)
        time.sleep(1.5)
        perf_during(ser, "rolagem vertical longa", ["240 290 240 110 450", "240 290 240 110 450", "240 110 240 290 450"])
        perf_during(ser, "voltar para Hoje", ["36 76 36 76 60"], settle=1.5)
    if mode == "mid":
        command(ser, "DRAG 400 220 160 220 2500", 1.2)
        snapshot(ser, "/tmp/todoist-panel-mid.png")
        time.sleep(2.5)
        command(ser, "DRAG 80 220 400 220 350", 1.5)
        time.sleep(1.5)
        command(ser, "DRAG 240 300 240 180 2500", 1.4)
        snapshot(ser, "/tmp/todoist-panel-vmid.png")
        time.sleep(2.0)
        command(ser, "DRAG 240 110 240 300 400", 1.5)
    if mode in ("all", "snap"):
        time.sleep(1.0)
        snapshot(ser, "/tmp/todoist-panel-snap.png")
    tail = read_until_quiet(ser, quiet=0.8, limit=3)
    for line in tail.splitlines():
        if line.startswith(("GLYPH", "DUE")):
            print("log:", line)
    ser.close()


if __name__ == "__main__":
    main()
