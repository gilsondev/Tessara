"""Force screen-rest phases and capture SNAP PNGs for each transition."""

import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from panel_probe import command, open_port, read_until_quiet, snapshot

OUT = pathlib.Path("/tmp/tessera-rest")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    ser = open_port(timeout=40)
    print(read_until_quiet(ser, quiet=2.5, limit=50))

    command(ser, "REST wake", 0.8)
    time.sleep(0.5)
    snapshot(ser, str(OUT / "01-awake.png"))

    print(command(ser, "REST saver", 0.8))
    time.sleep(1.2)  # let flip digits paint
    snapshot(ser, str(OUT / "02-saver.png"))

    # Give drift a moment; second saver snap after short wait
    time.sleep(2.0)
    snapshot(ser, str(OUT / "03-saver-hold.png"))

    print(command(ser, "REST fade", 0.4))
    time.sleep(3.5)  # mid-fade (~10s total)
    snapshot(ser, str(OUT / "04-fade-mid.png"))

    print(command(ser, "REST off", 0.6))
    time.sleep(0.6)
    snapshot(ser, str(OUT / "05-off.png"))

    # First drag should wake only (swallowed)
    print(command(ser, "DRAG 160 240 200 240 200", 0.1))
    time.sleep(0.5)
    snapshot(ser, str(OUT / "06-wake.png"))

    print("wrote", OUT)
    for p in sorted(OUT.glob("*.png")):
        print(" ", p, p.stat().st_size)


if __name__ == "__main__":
    main()
