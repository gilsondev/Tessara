# Portal UX + hero widgets (saudação / data) — design

**Data:** 2026-10-24  
**Decisão:** opção **A** — saudação e data como widgets próprios; relógio só hora (hero) + detalhe.

## Contexto

Após o lift Tessara e a opção C (copy no pacote clock), o portal misturava hub/Geral (board de widgets sumia) e o hello era demo. Queremos allowlist real no hero e configs web/device mais claras.

## Widgets

| ID | Papel | Tile na grade | Detalhe |
|----|--------|---------------|---------|
| `org.tessara.greeting` | Saudação no hero | não | não |
| `org.tessara.date` | Data curta no hero | não | não |
| `org.tessara.clock` | Hora no hero | opcional | Relógio (hora + data longa + sync) |
| todoist / weather / aitokens | como hoje | sim | sim |

- Remover hello do catálogo / registry (id legado pode existir só para CSV antigo ignorado).
- `clock_face::Summary` continua como **fonte de copy** (saudação/data/hora); cada widget decide o que o shell pinta.
- Home: timer de relógio consulta `dashboard` + registry e mostra só partes enabled.
- Defaults: greeting + date + clock **enabled** junto com todoist/weather/aitokens (cabe em `kMaxSlots`).

## Ciclo de vida / camadas

- Cada widget: `TsWidget` registrado; mount/unmount só quando o shell abre detalhe (greeting/date nunca montam UI).
- HTTP / time / settings só via `WidgetContext` (sem Arduino nos pacotes).
- Shell compõe hero e grade; runtime ownership do mount exclusive no `detail_host`.

## Portal (tessara.local)

1. Sidebar desktop: **Início** (hub + board “Na home”) + Geral / Agente / Todoist / Clima / AI Tokens.
2. Geral **não** esconde o board — board só no Início.
3. Descanso / tempo do relógio: `<input type="range">` + valor; snap no save pelos clamps existentes.
4. Booleanos (ex.: som): toggle CSS, não chips.
5. AI Tokens: última atualização + toggles Codex/Claude/Cursor (quais exibir). Sem clonar o detalhe.

## Device settings

1. Página Todoist: accent verde Tessara (não vermelho da marca).
2. Página AI Tokens: última atualização + toggles de providers + “Pedir cotas” (sem cards-clone do detalhe).

## Persistência providers AI

Campos em `DeviceSettings` (default true): `show_aitokens_codex`, `show_aitokens_claude`, `show_aitokens_cursor`. Home / detalhe / settings filtram fontes.

## Testes

- Native: layout ids, compose com greeting/date, clock_summary, settings roundtrip providers.
- Sim: home com partes do hero; screenshots.
- Device: smoke hub/widgets + abrir detalhes após flash.

## Todoist “perde a API”

**Não** é pedido periódico de token. O personal Bearer fica na NVS e é reutilizado em cada sync (`sync_minutes` só refetcha listas). O painel só mostra “falta o token” se a string na NVS estiver vazia (setup / `drop_token`).

Causa real observada no lab (2026-10-24): `todoist connect falhou heap_int≈41k` — handshake TLS do mbedTLS precisa de ~32 KB contíguos de DRAM interna; a stack de 24 KB do `net_worker` em DRAM interna deixava o heap fragmentado/insuficiente. Correção: stack do net/web em PSRAM, cliente TLS reutilizado, headers sem `String`, retries com log de `largest` free block.

## Fora de escopo

- OAuth Todoist; UX de 401 vs rede no widget (follow-up).
