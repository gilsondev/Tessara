# Simulator — host test harness

The simulator is **not** a demo or a second UI. It is how we exercise the **real** implementation without an ESP32 on the desk: the `painel` binary links the **same** firmware sources that ship on device.

Rule: if code runs on the device, the sim (or a native test) must exercise **that** code. Host-only forks under `simulator/host/` are glue (adapters, fixtures, CLI) — never a parallel product UI.

## Firmware vs glue

| Concern | Source of truth |
|---|---|
| Home / shell | `firmware/components/dashboard_shell/` |
| Widgets (Todoist, weather, AI Tokens, clock, …) | `firmware/widgets/…` |
| Layout / allowlist / compose / web embed | `pio test -e native` |
| Wi‑Fi / NVS / time / battery / SD | `simulator/host/` adapters + JSON fixtures |

## Finish gate

[`finish-gate.md`](finish-gate.md) · root `Makefile`:

```bash
make help
make finish-host   # test → e2e (no board)
make finish        # + flash
```

## Full host regression

```bash
./simulator/scripts/verify_all.sh
# or: make e2e
```

1. Native suite (domain, allowlist, compose, embed, …)
2. Build `painel`
3. Headless home (+ detail) screenshots for many fixtures

Artifacts: `/tmp/tessara-sim-out/` or `$SIM_OUT`.

## Interactive

Prerequisites: [`getting-started.md`](../getting-started.md) (SDL2, FreeType, LVGL via `pio pkg install`).

```bash
make sim
# equivalent:
cmake -S simulator -B simulator/build -DCMAKE_BUILD_TYPE=Release
cmake --build simulator/build -j
./simulator/build/painel --scenario normal --fixtures simulator/fixtures
```

- Click home tiles → detail
- Back / Esc on detail → home
- Esc on home → quit

## Headless screenshot

```bash
SDL_VIDEODRIVER=dummy ./simulator/build/painel \
  --scenario widgets_no_weather \
  --fixtures simulator/fixtures \
  --viewport 320x480 \
  --screen home \
  --screenshot /tmp/no-weather.bmp \
  --headless
```

`--screen`: `home` (default), `todoist`, `weather`, `aitokens`, … (see binary help / fixtures).

The binary **fails** if fixture layout does not match mounted tiles (compose).

## Fixtures (examples)

| Scenario | Exercises |
|---|---|
| `normal` | Wi‑Fi on, default widgets, fresh weather |
| `offline` | Wi‑Fi off, stale weather/AI |
| `sem_local` | Weather “set location” |
| `widgets_no_weather` | Dashboard without weather |
| `widgets_with_clock` | Clock tile in compose |
| `cold_offline` / `weather_timeout` / … | Fault paths |

## Flags

- `--viewport WxH` — default `320x480` (also `480x320`)
- `--scenario name`
- `--fixtures dir`
- `--screen …`
- `--screenshot path.bmp`
- `--settings dir` — isolated store (not device NVS)
- `--headless`
