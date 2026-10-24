# Repository structure

Aligned with the product architecture (see [`guides/architecture.md`](guides/architecture.md)). Product name: **Tessara**.

## Tree

```text
Tessara/
  firmware/                       # ESP32 app (PlatformIO src_dir)
    main.cpp
    components/
      hal/                        # Display / Input / Backlight / Power / Storage / …
      core_services/              # platform/, domain/, agent/, services/
      ui_kit/                     # kit · Ts* · palette · theme · fonts
      dashboard_shell/            # app/ + home / shell / settings / screensaver
      web_configurator/           # PROGMEM headers (from web/src)
      widget_sdk/                 # TsWidget · WidgetContext · manifest
      widget_runtime/             # registry · mount_exclusive · generation
    boards/jc3248w535/            # board adapter
    widgets/
      weather/
      clock/                      # hero + detail
      greeting/, date/            # hero-only
      aitokens/
      todoist/                    # optional (not MVP acceptance)
  agent/                          # Mac USB agent
  boards/*.json                   # PlatformIO board definition
  simulator/                      # LVGL9 + SDL2 · fixtures · verify_all.sh
  web/src/                        # portal → tools/embed_web.py
  test/test_native/
  docs/
    guides/                       # finish-gate, simulator, architecture, widget-authoring
    pt/                           # optional Portuguese mirrors
    adr/                          # repo ADRs
    superpowers/                  # dated feature specs/plans
  Makefile                        # finish gate
  tools/                          # bootstrap-env, flash, device, embed_web, …
  AGENTS.md
  CONTRIBUTING.md
  GLOSSARY.md
```

## Includes

Per-package `-I` in `platformio.ini`. Widgets must not include Arduino / Wi‑Fi / board headers (enforced by `test_widget_allowlist`).

## Layer status

| Layer | Status |
|---|---|
| `firmware/components` + `widgets` + `boards` | Active |
| `agent/` | Active (USB; AI Tokens) |
| `simulator/` | Active — `painel`, fixtures, `make e2e` |
| `web/` | Active — `web/src` → PROGMEM |
| Widget SDK / runtime | Active — mount / generation / http cancel |
| Widget allowlist | Enforced in native tests |
| Board toolchain | PlatformIO + Arduino + Arduino_GFX (ESP-IDF: future) |

## Onboarding

- Environment: [`getting-started.md`](getting-started.md)
- Docs index: [`README.md`](README.md)
