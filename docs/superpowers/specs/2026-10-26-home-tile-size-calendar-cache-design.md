# Home tile size + cache do Calendário

**Status:** approved (owner 2026-10-26)  
**Data:** 2026-10-26  
**Relacionado:** `2026-10-24-calendario-design.md`, `docs/guides/widget-authoring.md`, `docs/guides/architecture.md`

## Problemas

1. Tile Calendário volta a **“ligar agenda”** quando o catálogo/USB ainda não respondeu, mesmo com eventos já recebidos.
2. Tile mostra só 1 linha; falta altura configurável para listar mais eventos — e o padrão deve servir a **outros widgets**.

## Cache / link (Calendário)

| Regra | Comportamento |
|---|---|
| Fonte de verdade dos eventos | Só `CALENDAR\tv1\t…` via `calendar::apply_line` |
| “ligar agenda” | Só se **nunca** houve `ready` com `linked=1` **e** catálogo não confirma seleção |
| Catálogo vazio / Agente offline | **Não** zera `linked` nem apaga eventos já em cache |
| Catálogo com seleção / contas | Pode marcar `linked=true` |
| `CALENDAR` com `linked=0` | Resposta externa real → aceita (mostra ligar agenda / livre) |
| Stale | Mantém eventos; tile não altera o padrão da linha (só glifo/estado) |

## Altura do tile (genérico)

Setting namespaced (todos os widgets que optarem):

| Key | Valores | Default |
|---|---|---|
| `home_tile_size` | `S` · `M` · `L` · `XL` | `M` |

| Size | Linhas de conteúdo (Calendário = eventos) | Altura mínima sugerida |
|---|---|---|
| S | 1 | ~56 px body |
| M | 2 | ~88 |
| L | 3 | ~120 |
| XL | 4 | ~152 |

API compartilhada: `firmware/components/core_services/domain/home_tile_size.h` (+ `.cpp`).

Manifest (`WidgetManifest`):

- `bool supports_home_tile_size` — **todas** as superfícies de config mostram chips só se true.

Shell:

- Lê `home_tile_size` do widget ao montar/atualizar o tile.
- Não hardcoda produto além do layout (altura + slots de linhas).
- Ao mudar o setting → reaplicar altura + `paint` (sem reboot).

### Onde o usuário escolhe a altura (obrigatório)

A mesma chave `home_tile_size` (NVS namespaced no aparelho) deve ser editável em **três** lugares — mesmo significado, mesmos valores S–XL:

| Superfície | Quando | Como |
|---|---|---|
| **Portal web** (`tessera.local`) | Sempre que o Wi‑Fi do aparelho estiver ok | Chips “Altura do tile” na página do widget (`/api/widget_settings`) |
| **Agente (agent-ui)** | Mac ativo + painel alcançável (USB ou Wi‑Fi) | Mesmos chips na página/seção do widget; IPC → portal `widget_settings` |
| **Painel no aparelho** | Sem celular/Mac | Configurações → página do widget |

Sem cabo / sem rede: o Agente **não** inventa altura local — mostra estado “painel offline” e não persiste altura só no Mac (fonte de verdade = aparelho).

Calendário (1º consumidor):

- Portal Agenda: chips “Altura do tile” (já).
- Agente: seção Agenda / widgets com os mesmos chips quando o painel responde.
- Painel Configurações → Calendário: mesmos chips.
- Tile e detalhe: só eventos **de hoje** (`day_offset == 0`) — sem amanhã em diante.
- Até N linhas no tile, **um por linha**, `HH:MM · título` (all-day: `Dia todo · título`).
- Ordem: **Dia todo** (só all-day de hoje) antes dos horários; agentd ordena o cache assim no refresh.

## Widgets futuros

Qualquer tile full-width (AI Tokens, Mac, etc.) pode setar `supports_home_tile_size` e interpretar S–XL (ex.: mais fontes AI, mais modos Focus). Cada um que optar **deve** expor chips no portal **e** no Agente (não só num dos dois).

## Fora

- Drag-resize livre em px.
- Persistência de eventos em flash/SD (cache = RAM do boot atual + última linha CALENDAR).
- Altura configurável só no Agente sem sync com o aparelho.
