# POC structure (Tessara-shaped)

Incuba a forma de `Terrara/docs/specs/ARCHITECTURE.md` neste Labs. **Estratégia:** amadurecer aqui e, quando maduro, promover (cópia/extração) para o repositório Tessara — ver `docs/guides/promotion-to-tessara.md`. ADR-009 no Terrara continua válido até a promoção ser documentada lá.

Canonical product name: **Tessara**. This POC sometimes still says Tessara in strings.

## Tree

```text
todoist-panel/                    # Labs POC root
  firmware/                       # ESP32 application (PlatformIO src_dir)
    main.cpp
    components/
      hal/                        # Display/Touch/Board ports
      core_services/              # platform/, domain/, agent/, services/
      ui_kit/                     # ui/kit · palette · theme · fonts
      dashboard_shell/            # app/ + home/shell/settings/screensaver
      web_configurator/           # PROGMEM headers (gerados de web/src)
      widget_sdk/                 # TsWidget registry / mount
      widget_runtime/             # weather via runtime; Wi‑Fi peel
    boards/jc3248w535/            # board adapter (pins / Arduino_GFX)
    widgets/
      weather/                    # weather/ + ui/
      aitokens/                   # aitokens/ + ui/
      todoist/                    # POC only — out of Tessara MVP acceptance
      clock/                      # registered stub
      clock/                      # hora (hero) + detalhe; copy via clock_face
      greeting/, date/            # hero-only (saudação / data curta)
  agent/                          # Mac Tessara Agent (monorepo sibling)
    src/agent/
  boards/*.json                   # PlatformIO board definition (repo root)
  simulator/                      # LVGL9+SDL2 painel — fixtures + verify_all.sh
    src/                          # main, scenario, sim_home, sim_detail, sim_app
    host/                         # daytime/power/settings/log adapters
    fixtures/                     # JSON scenarios
    scripts/verify_all.sh
  web/                            # HTML editável → tools/embed_web.py → PROGMEM
    src/
  tests/test_native/
  docs/
    guides/simulator.md           # como rodar / verificar sem aparelho
    guides/finish-gate.md         # make test / e2e / flash / device
    guides/promotion-to-tessara.md  # incubação Labs → repo Tessara
    guides/architecture-lift.md     # features → Widget SDK / widgets desacoplados
  hardware/                       # CAD / stand — not firmware
  Makefile                        # finish gate: test / e2e / flash / device / finish
  tools/
    flash.sh                      # build + upload no JC3248
    device.sh                     # monitor USB / SNAP para teste manual
    finish.sh                     # wrapper → make finish
  AGENTS.md                       # finish gate para agentes
  docs/guides/finish-gate.md      # doc das tasks Make
  .cursor/rules/finish-gate.mdc   # regra alwaysApply
```

## Include strategy

Old prefixes (`"hal/…"`, `"ui/kit.h"`, `"weather/…"`) still work via `-I` into each package root (see `platformio.ini`). Physical folders follow Tessara component names.

## What this validates

| Idea | Status in POC |
|---|---|
| `firmware/` + `components/` + `widgets/` + `boards/` | Applied |
| Sibling `agent/` Mac package | Applied |
| `simulator/` early | **Live** — SDL+LVGL `painel`, fixtures, `scripts/verify_all.sh` (sem ESP32) |
| `web/{src,assets}` extracted | **Live** — `web/src/*.html` + `tools/embed_web.py` → PROGMEM headers |
| `widget_sdk` / `widget_runtime` | **v0 live** — mount/generation; weather via runtime; controller peels Wi‑Fi via `DeviceInfo`/`net_worker` |
| Widget allowlist | **Enforced** — `test_widget_allowlist` scans `firmware/widgets/**` (no Arduino/Wi‑Fi/heap/boards) |
| Dashboard layout / enable widgets | **Live** — NVS + portal toggles; home = `compose` + **rebuild na hora** (sem reboot) |
| Widget packages as TsWidget | weather + todoist + aitokens + clock registered in `register_builtin` |
| ESP-IDF primary | Not yet — still Arduino + Arduino_GFX (M0 spike ok) |

## Terrara mapping

| Tessara proposal | POC location |
|---|---|
| `components/hal` | `firmware/components/hal` |
| `components/board_support` | `firmware/boards/jc3248w535` |
| `components/core_services` | `firmware/components/core_services` |
| `components/ui_kit` | `firmware/components/ui_kit` |
| `components/dashboard_shell` | `firmware/components/dashboard_shell` |
| `components/web_configurator` | `firmware/components/web_configurator` |
| `widgets/{clock,weather,…}` | `firmware/widgets/*` |
| Mac agent | `agent/` (not in original sketch; added for USB MVP) |
