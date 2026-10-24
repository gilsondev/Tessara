# Simulator (Tessara)

Host **test harness** for the real firmware UI — not a demo. Links the same sources as the device (LVGL9 + SDL2 + fixtures).

Guide: [`docs/guides/simulator.md`](../docs/guides/simulator.md)  
Environment: [`docs/getting-started.md`](../docs/getting-started.md)  
Finish gate: `make help` → `make finish-host`

## Prerequisites

- SDL2 + FreeType + CMake + Ninja + pkg-config (see getting-started for macOS/Linux)
- `.venv` with PlatformIO and `pio pkg install -e jc3248w535` (LVGL path used by CMake)

```bash
./tools/bootstrap-env.sh
make sim
```

## Headless / E2E

```bash
make e2e
# artifacts: $SIM_OUT (default /tmp/tessara-sim-out/)
```
