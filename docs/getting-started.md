# Getting started

Set up a **host** development environment (macOS or Linux), run tests and the simulator **without** a panel, then optionally flash a JC3248W535.

Portuguese: [`pt/getting-started.md`](pt/getting-started.md)

## What you need

| Goal | Required |
|---|---|
| Native tests + headless E2E | Python 3.10+, PlatformIO in `.venv`, CMake, Ninja, pkg-config, SDL2, FreeType |
| Interactive simulator | Same as above + a display (X11/Wayland/macOS window) |
| Flash / USB monitor | Working USB cable + board; serial port differs by OS |

Hardware is **optional** for most firmware work. Prefer `make finish-host` first.

## 1. Clone

```bash
git clone <your-fork-or-url> Tessera
cd Tessera
```

Product specs may also exist in a sibling `Terrara` docs tree used by the owner. This repository’s guides under `docs/` are enough to build, test, and author widgets.

## 2. OS packages

### macOS (Homebrew)

```bash
brew install cmake ninja pkg-config sdl2 freetype python3
```

Optional (device tooling already covered by PlatformIO): nothing else required for host work.

### Linux (Debian / Ubuntu)

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build pkg-config \
  libsdl2-dev libfreetype-dev \
  python3 python3-venv python3-pip \
  git
```

### Linux (Fedora)

```bash
sudo dnf install -y \
  gcc gcc-c++ make cmake ninja-build pkgconf-pkg-config \
  SDL2-devel freetype-devel \
  python3 python3-pip \
  git
```

### Linux (Arch)

```bash
sudo pacman -S --needed \
  base-devel cmake ninja pkgconf \
  sdl2 freetype2 \
  python python-pip \
  git
```

## 3. Python venv + PlatformIO

From the repository root:

```bash
python3 -m venv .venv
.venv/bin/pip install -U pip platformio

# Pull firmware libs (LVGL, ArduinoJson, …) used by device + simulator
.venv/bin/pio pkg install -e jc3248w535
```

The Makefile expects PlatformIO at `.venv/bin/pio`. Do not commit `.venv/` or `.pio/`.

**Bootstrap helper** (same steps):

```bash
./tools/bootstrap-env.sh
```

## 4. Verify without hardware

```bash
make help
make finish-host
```

This runs:

1. `pio test -e native` — domain, allowlist, compose, weather parse, …
2. Simulator build + headless screenshots for many fixtures

Artifacts land in `/tmp/tessera-sim-out/` (override with `SIM_OUT=…`). Inspect PNGs/BMPs when UI changes.

Interactive window:

```bash
make sim
# or: make sim-build && SCENARIO=normal make sim-run
```

Default viewport is **320×480** (portrait).

## 5. Device (optional)

1. Connect the JC3248 over USB. Confirm the serial device:
   - macOS: often `/dev/cu.usbmodem…`
   - Linux: often `/dev/ttyACM0` or `/dev/ttyUSB0` (add your user to the `dialout` group if needed)
2. Adjust `upload_port` / `monitor_port` in `platformio.ini` (or pass `-t upload` with PlatformIO port flags via `tools/flash.sh` if your checkout supports it).
3. Only after `make finish-host` is green:

```bash
make flash
make device          # USB serial monitor
# make device-snap   # framebuffer → $SIM_OUT/device-snap.png
```

Wi‑Fi / tokens: prefer the **portal** / NVS. Optional local seed only:

```bash
cp include/secrets.example.h include/secrets.h
# edit secrets.h — never commit it
```

## 6. Common failures

| Symptom | Likely fix |
|---|---|
| `pio: No such file` | Create `.venv` and install PlatformIO (step 3) |
| CMake: LVGL not found | Run `.venv/bin/pio pkg install -e jc3248w535` |
| `pkg_check_modules(SDL2)` fails | Install `sdl2` / `libsdl2-dev` + `pkg-config` |
| Headless E2E needs display libs | SDL2 must still be installed; `SDL_VIDEODRIVER=dummy` is set by `verify_all` |
| Linux permission denied on serial | `sudo usermod -aG dialout $USER` then re-login |
| Wrong upload port | List ports (`ls /dev/cu.usb*`, `ls /dev/ttyACM*`) and update `platformio.ini` |

## Agent (Mac)

- Agent v2 (em construção): [guides/agent-v2-dev.md](guides/agent-v2-dev.md) — `mise install`
- Agent legacy (Python): `cd agent && …` ou `mise run agent-legacy-run`

## Next

- Architecture: [`guides/architecture.md`](guides/architecture.md)
- New widgets: [`guides/widget-authoring.md`](guides/widget-authoring.md)
- Contribution rules: [`../CONTRIBUTING.md`](../CONTRIBUTING.md)
