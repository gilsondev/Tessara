# Tessara

Painel de parede modular para ESP32 (JC3248W535): home de widgets, detalhe por widget, portal no celular e Agente no Mac (USB).

Specs de produto (ARCHITECTURE, WIDGET-SDK, SIMULATOR, …): `~/Developer/Projects/gilsondev/Terrara/docs/specs/`.

## Layout

Ver [`docs/STRUCTURE.md`](docs/STRUCTURE.md).

| Path | Papel |
|---|---|
| `firmware/` | App do aparelho (`components/`, `widgets/`, `boards/`) |
| `agent/` | Agente Tessara no Mac (USB; AI Tokens = 1ª capability) |
| `simulator/` | Painel SDL + LVGL9, fixtures e `verify_all` |
| `web/` | HTML do portal → embed PROGMEM |
| `boards/*.json` | Perfil PlatformIO da placa |
| `docs/` | Guides, ADRs do repo, specs/planos de features |

## Build / flash

```bash
python3 -m venv .venv && .venv/bin/pip install -U platformio
.venv/bin/pio run -e jc3248w535
make flash          # ou ./tools/flash.sh
make device         # monitor USB
```

Segredos: copie `include/secrets.example.h` → `include/secrets.h` só se quiser seed de Wi‑Fi/token; o caminho normal é o portal / NVS.

## Finish gate

```bash
make help
make finish-host    # test + e2e (sim)
make finish         # + flash
```

Detalhe: [`docs/guides/finish-gate.md`](docs/guides/finish-gate.md). Agentes: [`AGENTS.md`](AGENTS.md).

## Agent (Mac)

```bash
cd agent && PYTHONPATH=src python3 -m agent
./pack_sd.sh /Volumes/YOUR_SD
```

## Escopo

- **Aceite MVP de produto:** relógio + clima + shell/SDK/sim/portal.
- **No repo (widgets/extras):** Todoist, AI Tokens, screen rest, heroes — úteis, não bloqueiam o MVP.
- **Fora do aceite atual:** Calendar, marketplace, OTA IDF completo.
