# Screenshots

Committed PNGs for the root README (320×480 portrait).

## Regenerate from the simulator

```bash
make finish-host   # writes BMPs under $SIM_OUT (default /tmp/tessera-sim-out/)
# convert examples:
sips -s format png /tmp/tessera-sim-out/normal.bmp --out docs/images/normal.png
sips -s format png /tmp/tessera-sim-out/detail-weather.bmp --out docs/images/detail-weather.png
sips -s format png /tmp/tessera-sim-out/detail-clock.bmp --out docs/images/detail-clock.png
```

## On-device

```bash
make device-snap   # → $SIM_OUT/device-snap.png
cp /tmp/tessera-sim-out/device-snap.png docs/images/device-first-use.png
```

Refresh these when home / detail UI changes in a visible way.
