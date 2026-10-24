# Simulador = harness de teste (sem aparelho)

O simulador **não** é demo nem mock visual. É a forma de **testar a implementação real** quando não há ESP32 na mesa: o binário `painel` linka as **mesmas fontes** que vão para o aparelho.

Regra: se o código roda no device, o sim deve exercitar **esse** código (ou o teste nativo). Fork/`sim_*` só existe como cola (adapters host, fixtures, CLI) — nunca como segunda UI.

## O que é firmware vs cola

| Camada | Fonte de verdade |
|---|---|
| Home | `home.cpp` |
| Tarefas (chips + slide + rows) | `todoist_panel` + `task_page` + `task_store` + `bitmaps` |
| Clima | `weather_page` + `weather_icons` |
| AI Tokens (barras + listras) | `aitokens_page` |
| Layout / allowlist / compose / web embed | `pio test -e native` |
| Wi‑Fi / NVS / hora / bateria / SD | adapters em `simulator/host/` + fixtures JSON |

## Finish gate (obrigatório ao fechar implementação)

Doc completa: [`finish-gate.md`](finish-gate.md). Tasks no **`Makefile`**:

```bash
cd todoist-panel
make help
make finish        # test → e2e → flash
make finish-host   # test → e2e (sem placa)
```

Ordem fixa: `make test` → `make e2e` (inspecionar `$SIM_OUT`) → `make flash`.

## Comando de regressão (host)

```bash
cd todoist-panel
./simulator/scripts/verify_all.sh
```

1. Suite nativa (domínio + allowlist + compose + embed)  
2. Build do `painel`  
3. Screenshots headless de home + detalhes (código real)  

Artefatos: `/tmp/tessara-sim-out/` (ou `$SIM_OUT`).

## Interativo (gesto / chip / slide)

```bash
cmake -S simulator -B simulator/build -DCMAKE_BUILD_TYPE=Release
cmake --build simulator/build -j
./simulator/build/painel --scenario normal --fixtures simulator/fixtures
```

- Home → tiles abrem as páginas reais  
- **Engrenagem** → `settings_panel` real (Geral, Todoist, Clima, AI Tokens, Agente; chips, QR, Pedir cotas)  
- **Tarefas**: chips Hoje/Casa/Computador/Trabalho + swipe (mesmos filtros do device)  
- **AI Tokens**: barras com listras do `aitokens_page` (display no formato do agente)  
- Voltar (‹) ou **Esc** → home / fecha ajustes; **Esc** na home → sair  

## Screenshot

```bash
SDL_VIDEODRIVER=dummy ./simulator/build/painel \
  --fixtures simulator/fixtures \
  --scenario normal \
  --screen todoist \
  --screenshot /tmp/todoist.bmp \
  --headless
```

`--screen`: `home` \| `todoist` \| `weather` \| `aitokens` \| `settings`.

## Fixtures

JSON em `simulator/fixtures/`. O cenário `normal` espelha o pipeline real:

| Fonte | Formato no fixture |
|---|---|
| Todoist filters | `todoist.lists[]` com `name`/`query`/`tasks[]` (`id`, `content`, `priority` 1–4, `meta` = due/prazo) |
| Home count | `todoist.total` = soma das tarefas das listas (ou omitir para calcular) |
| Clima | `weather.*` → `weather::View` |
| AI Tokens | `codex_5h`/`codex_week`, `claude_pct`, `cursor_a`/`cursor_b` (+ resets) como no agente |
| Rede | `wifi.ssid` / `wifi.ip` para o card e QR nas configurações |
| Agente | `agent.ready` / `ok` / `pack_on_sd` |
| Relógio | `clock.month` é `tm_mon` (0–11); `wday` é `tm_wday` |

## Lacunas ainda abertas (próximos alvos de teste)

Enquanto não linkados, **não** estão cobertos 100% pelo sim:

- Setup / screensaver / loader overlays  
- Portal web ao vivo (só embed + nativos)  
- Agent/Bridge USB real (fixtures de snapshot)  

Tudo o que entrar no firmware de UI deve migrar para o sim **antes** de depender do aparelho para validar.
