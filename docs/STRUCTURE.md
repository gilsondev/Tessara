# Estrutura do repositório Tessara

Alinhada a `Terrara/docs/specs/ARCHITECTURE.md`. Nome do produto: **Tessara**.

## Árvore

```text
Tessara/
  firmware/                       # app ESP32 (PlatformIO src_dir)
    main.cpp
    components/
      hal/                        # Display / Input / Backlight / Power / Storage / …
      core_services/              # platform/, domain/, agent/, services/
      ui_kit/                     # kit · Ts* · palette · theme · fonts
      dashboard_shell/            # app/ + home / shell / settings / screensaver
      web_configurator/           # headers PROGMEM (gerados de web/src)
      widget_sdk/                 # TsWidget · WidgetContext · manifest
      widget_runtime/             # registry · mount_exclusive · generation
    boards/jc3248w535/            # adapter da placa
    widgets/
      weather/
      clock/                      # hero + detalhe
      greeting/, date/            # hero-only
      aitokens/
      todoist/                    # widget opcional (não é aceite MVP)
  agent/                          # Agente Tessara (Mac, USB)
  boards/*.json                   # definição PlatformIO
  simulator/                      # LVGL9 + SDL2 · fixtures · verify_all.sh
  web/src/                        # portal → tools/embed_web.py
  test/test_native/
  docs/
    guides/                       # finish-gate, simulator, architecture
    adr/                          # decisões deste repo
    superpowers/                  # specs / plans / shots de features
  Makefile                        # finish gate
  tools/                          # flash, device, probe, embed_web, …
  AGENTS.md
  GLOSSARY.md
```

## Includes

`-I` por pacote em `platformio.ini`. Widgets não incluem Arduino / Wi‑Fi / board (enforce: `test_widget_allowlist`).

## Estado das camadas

| Camada | Status |
|---|---|
| `firmware/components` + `widgets` + `boards` | Ativo |
| `agent/` | Ativo (USB; AI Tokens) |
| `simulator/` | Ativo — `painel`, fixtures, `make e2e` |
| `web/` | Ativo — `web/src` → PROGMEM |
| Widget SDK / runtime | Ativo — mount/generation/http cancel |
| Allowlist de widgets | Enforce nos testes nativos |
| Dashboard / toggles | NVS + portal; home recompõe na hora |
| Toolchain placa | PlatformIO + Arduino + Arduino_GFX (ESP-IDF: evolução futura) |

## Mapa ↔ specs Terrara

| Spec / ideia | Neste repo |
|---|---|
| `components/hal` | `firmware/components/hal` |
| board support | `firmware/boards/jc3248w535` |
| core services | `firmware/components/core_services` |
| UI Kit | `firmware/components/ui_kit` |
| dashboard shell | `firmware/components/dashboard_shell` |
| web configurator | `firmware/components/web_configurator` + `web/` |
| widgets | `firmware/widgets/*` |
| Mac agent | `agent/` |
