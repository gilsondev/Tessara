# Mac — Fase 1 Mídia (design)

**Status:** approved (grilling 2026-10-25) → implement  
**Data:** 2026-10-25  
**Relacionado:** `2026-10-24-tessera-agent-capabilities-design.md` (mapa), `2026-10-24-tessera-agent-design.md`  
**Glossário:** Mac, Cartão Mac, Tela Mac, Agora tocando, Mac offline, Agente Tessara

## Problema

O painel já recebe cotas e agenda do Mac via USB. Falta o companion de mesa mais óbvio: **ver o que está tocando e pausar/avançar sem ir ao notebook**.

## Decisões de produto (grilling)

| Tema | Decisão |
|---|---|
| Forma | **Um** cartão/tela **Mac** (não widget “Mídia” separado) |
| Trabalho | Glance-and-act; não remote desktop |
| Ordem | Mídia → Focus → Lock; sleep / volume / brilho / capa **fora** desta entrega |
| Transporte | **Só USB** (mesmo canal do Agente) |
| Cartão | **Só leitura** (título/artista ou vazio); controles **só na Tela Mac** |
| Controles | play/pause, previous, next |
| Sessões | Qualquer Agora tocando do sistema; se várias: a que **toca**, senão a **mais recente**; pausado **ainda mostra** a faixa |
| Vazios | **Mac offline** ≠ **nada tocando**; cartão **permanece** na home offline |
| Push | On change + heartbeat ~5–10 s |
| Rótulo | UI **“Mac”** na Fase 1; modelo da máquina pode entrar depois |
| Capa | Evolução **depois** de Focus/Lock |

Este doc **substitui** a UI da Fase 1 no mapa de capabilities (lá o card tinha play/pause na home e um card “Mídia” separado). Protocolo `MEDIA` do mapa **permanece**.

## Abordagens

| | Abordagem | Prós | Contras |
|---|---|---|---|
| **A** | Widget `org.tessara.media` só mídia | Isolado | Home polui; Focus/Lock forçam segundo/terceiro card |
| **B (recomendada)** | Widget **`org.tessara.mac`**: Fase 1 só bloco mídia; Focus/Lock entram depois na mesma tela | Casa com glossário; um cartão | Pacote cresce com o tempo |
| **C** | Embutir mídia no AI Tokens | Zero tile novo | Mistura conceitos; glossário proíbe |

**Escolha: B.**

## Arquitetura

```
Mac apps ──► Agente (capabilities/media) ──USB──► console ──► mac::snapshot ──► Cartão/Tela Mac
                 ▲                                      │
                 └──────── MEDIA? / PLAYPAUSE/NEXT/PREV ┘
```

| Camada | Responsabilidade |
|---|---|
| `agent/capabilities/media/` | Ler Agora tocando; `build_line()`; executar PLAYPAUSE/NEXT/PREV; degradar `ok=0` |
| `agent/runtime.py` | Push MEDIA no turno USB; despachar `MEDIA?` e `MEDIA\t…` |
| `firmware/widgets/mac/` | Parse `MEDIA`, home tile, página detalhe; **sem** APIs Mac |
| Shell / controller | `on_media` → `apply_line` → paint; Serial out dos comandos |
| Portal | Fora do corte (só doc de permissões no README do agente) |

## Protocolo

Linhas `\n`, campos `\t` (igual AITOKENS/CALENDAR).

### Push (Mac → painel)

```
MEDIA\tv1\t1\tplaying|paused|stopped\t<title>\t<artist>\t<app>
MEDIA\tv1\t0\t\t\t\t
```

| Campo | Notas |
|---|---|
| `ok` | `1` = linha válida; `0` = sem permissão / falha de leitura (não confundir com “nada tocando”) |
| `state` | `playing` / `paused` / `stopped` — `stopped` + ok=1 = sessão ausente (“nada tocando”) |
| `title`, `artist`, `app` | Sem `\t`/`\n`; truncar no agente (~40/40/24 chars) |

### Pedido / ação (painel → Mac)

```
MEDIA?
MEDIA\tPLAYPAUSE
MEDIA\tNEXT
MEDIA\tPREV
```

Agente responde a `MEDIA?` com um push imediato. Comandos de ação não precisam de ACK de linha; o próximo push reflete o estado.

### Frescor

- Push: mudança de faixa/estado **ou** heartbeat ≤ 10 s.
- Painel: `ready` após primeira linha válida; **stale** se sem push ~45 s (`kStaleMs`); UI de stale ≠ Mac offline.
- **Mac offline:** sem `AGENT` ok recente (já existe no estado do agente) — cartão mostra offline mesmo sem linha MEDIA.

## Domínio no firmware (`mac::`)

```cpp
enum class MediaState : uint8_t { Stopped, Playing, Paused };

struct Media {
  bool ok = false;       // last line ok flag
  MediaState state = MediaState::Stopped;
  char title[41];
  char artist[41];
  char app[25];
};

struct View {
  bool ready = false;
  bool stale = false;
  uint32_t received_ms = 0;
  Media media;
};
```

`apply_line` só aceita prefixo `MEDIA`. Focus/Lock **não** entram neste struct na Fase 1.

## UI

### Cartão Mac (home)

- Título do tile: **Mac**
- Corpo: se agente offline → “Mac offline”; senão se `stopped`/sem título → “Nada tocando”; senão `title` (+ artista se couber) e indício playing/paused
- Toque → abre Tela Mac
- **Sem** botões de mídia

### Tela Mac

- Barra de subsistema + bloco Agora tocando (título, artista, app opcional, estado)
- Três controles: previous · play/pause · next → `Serial.println("MEDIA\t…")`
- Ao abrir: `MEDIA?` para aquecer
- Sem capa; sem stubs Focus/Lock

## Agente macOS (leitura / ação)

- Isolar em `capabilities/media/`. Preferir API de Now Playing do sistema (MediaRemote / equivalente); controles via media keys / mesma stack.
- Sem permissão ou falha → `ok=0` (daemon continua).
- Documentar Accessibility (se necessária) no `agent/README.md`.
- **Sem** shell arbitrário, volume, brilho, abrir apps.

## Testes

| Camada | Cobertura |
|---|---|
| Native | `apply_line` (playing/paused/stopped/ok=0), `refresh_age` / stale |
| Agent | unit do encoder de linha + parse de comando (mock nowplaying) |
| Device | música no Mac → cartão → pause/next na Tela Mac |

## Fora de escopo (Fase 1)

Focus, Lock, Sleep, capa, volume/brilho, Wi‑Fi remoto, apps/janelas, clipboard, modelo no rótulo, settings no portal.

## Critérios de pronto

1. Com Agente USB e faixa tocando: Cartão Mac mostra título; Tela Mac pausa/avança/volta.
2. USB desligado: cartão mostra Mac offline (não some).
3. Agente ok, sem sessão: “Nada tocando”.
4. `make test` + `make e2e` verdes; flash verifica no JC3248.

## Self-review

- Sem TBD de protocolo/UI Fase 1.
- Consistente com glossário (cartão só leitura).
- Escopo = uma capability + um widget Mac; Focus fica para spec posterior.
