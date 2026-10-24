# Simulador desktop (Tessara lab)

**Harness de teste** da implementação real — não é demo. Linka as mesmas fontes do firmware (`home`, `todoist_panel`, `weather_page`, `aitokens_page`, …) no Mac com LVGL9+SDL2 + fixtures.

Guia: [`docs/guides/simulator.md`](../docs/guides/simulator.md).

**Finish gate:** `make help` na raiz — `make test` → `make e2e` → `make flash` (atalho `make finish`). Doc: [`docs/guides/finish-gate.md`](../docs/guides/finish-gate.md).

## Pré-requisitos

- SDL2 + FreeType (`brew install sdl2 freetype`)
- CMake + Ninja (`brew install cmake ninja`)
- Dependências PlatformIO do painel (LVGL/ArduinoJson): `.venv/bin/pio pkg install -e jc3248w535`

## Build e janela

```bash
cmake -S simulator -B simulator/build -DCMAKE_BUILD_TYPE=Release
cmake --build simulator/build -j
./simulator/build/painel --scenario normal --fixtures simulator/fixtures
```

- Clique nos tiles **Tarefas / Clima / AI Tokens** → detalhe  
- Botão ‹ ou **Esc** no detalhe → home  
- **Esc** na home → sair  

## Screenshot / headless

```bash
SDL_VIDEODRIVER=dummy ./simulator/build/painel \
  --scenario widgets_no_weather \
  --fixtures simulator/fixtures \
  --viewport 320x480 \
  --screen home \
  --screenshot /tmp/no-weather.bmp \
  --headless
```

`--screen`: `home` (default), `todoist`, `weather`, `aitokens`.

O binário **falha** se o layout do fixture não bater com os tiles montados (compose).

## Verificar tudo sem aparelho

```bash
./simulator/scripts/verify_all.sh
```

Isso roda:

1. `pio test -e native` (allowlist, dashboard, compose, web embed, domínio…)
2. Build do simulador
3. Home de todos os fixtures (BMP + asserts de compose)
4. Telas de detalhe do cenário `normal` (todoist / weather / aitokens)

## Fixtures

| Cenário | O que exercita |
|---|---|
| `normal` | Wi‑Fi on, 3 widgets default, clima fresco, lista de tarefas |
| `offline` | Wi‑Fi off, clima/AI stale |
| `sem_local` | Clima “definir local” |
| `sem_bateria` | Bateria oculta (`-1`), AI “ligar Bridge” |
| `widgets_no_weather` | Dashboard sem Clima (toggle Tessara) |
| `widgets_with_clock` | Tile Relógio habilitado no compose |
| `weather_loading` | Clima carregando + Todoist pending |

## Flags

- `--viewport WxH` — default `320x480` (também `480x320`)
- `--scenario name`
- `--fixtures dir`
- `--screen home\|todoist\|weather\|aitokens`
- `--screenshot path.bmp`
- `--settings dir` — store isolado (não é NVS do aparelho)
- `--headless`
