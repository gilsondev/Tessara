# Todoist via Agent (painel leve)

**Data:** 2026-10-26  
**Status:** implementado no tronco — agentd faz HTTPS; painel só pinta + fecha via USB  
**Guia operacional / regressão:** [`docs/guides/todoist-agent.md`](../../guides/todoist-agent.md)  
**Relacionado:** Agenda (`caps-calendar`), capabilities map, logs DRAM/`todoist TLS sem DRAM`

## Problema

O painel chama `api.todoist.com` com mbedTLS. Logs reais:

- `todoist TLS sem DRAM … largest≈21KB (mín=28672)` — free total ~60KB, bloco contínuo insuficiente
- ciclo `TLS ok` → `TLS drop (DRAM baixa)` — keep-alive + UI/USB fragmentam o heap interno

Payloads já estão em PSRAM (`task_store`, body 160KB). **PSRAM não resolve o handshake.** SD como cache ajuda UX, mas **não elimina TLS no ESP32**. Com agentd estável (Agenda, AI Tokens, self-update), a solução definitiva é a mesma da Agenda: **Mac busca, painel recebe snapshot.**

## Decisão

| Camada | Papel |
|---|---|
| **agentd** (`caps-todoist`) | Token, REST Todoist, cache em disco no Mac, push USB, close |
| **Painel** | `task_store` em PSRAM (working set), UI, closes otimistas → USB |
| **Portal / Agent-ui** | Configurar token + intervalo + listas (como hoje), persistindo no Mac |

**Não** guardar “todo o Todoist” só em PSRAM como verdade. **Não** depender de SD no painel para sync. SD no painel fica opcional (boot sem cabo) — fase 2.

Atualiza o mapa de capabilities: Todoist **sai** de “continuam Wi‑Fi no painel”.

## Protocolo USB (espelha CALENDAR / AITOKENS)

| Linha | Direção | Papel |
|---|---|---|
| `TODOIST\tv1\tok\tlinked\t…` | Mac→painel | snapshot (views + tasks) |
| `TODOIST?` | painel→Mac | pedir refresh imediato |
| `TODOIST_CLOSE\tid` | painel→Mac | fechar tarefa |
| `TODOIST_CLOSE\tv1\tok\|err\tid` | Mac→painel | ack do close |
| `TODOIST_TOKEN\t…` / IPC | Mac | token só no Mac (`~/.tessera/todoist/`) |

### Snapshot (multi-linha — buffer USB ~4KB/linha no painel)

```
TODOIST\tv1\tok\tlinked\t<view_count>\t<name>\t<query>…   # catálogo
TODOIST_TASKS\tv1\t<index>\t<n>\t<id>\t<prio>\t<title>\t<meta>…  # por lista (todas, ≤60)
TODOIST_END\tv1\tok\tlinked\t<view_count>                  # commit no painel
```

- `ok=1` bridge vivo; `linked=1` se token no Mac (`~/.tessera/todoist/token`)  
- Boot: painel pode enviar `TODOIST_TOKEN\t…` uma vez (migração NVS→Mac) + `TODOIST?`  
- Cap wire: **mesmo teto do Wi‑Fi** — 8 views × **60** tasks (`kMaxTasks`); linhas longas (~3.5KB) ou pedaços com append no painel; títulos truncados no fio  
- Push quando mudar **ou** heartbeat ~45s; refresh REST no Mac a cada `sync_minutes`

### Close

1. UI remove da lista (otimista)  
2. `TODOIST_CLOSE\t<id>`  
3. agentd `POST /tasks/{id}/close`; ack  
4. Sem Wi‑Fi no painel; fila de closes no painel se cabo cair (retry ao reconnect)

## agentd

Novo crate `agentd/crates/caps-todoist` (padrão `caps-calendar`):

- Token: `~/.tessera/todoist/token` (ou json); **nunca** no log / Doctor detail  
- Cache: `~/.tessera/todoist/cache.json` (views+tasks+fetched_at)  
- HTTP: `reqwest`/`ureq` no Mac — sem limite de DRAM do ESP  
- IPC: estender `/v1/panel/token` para gravar no Mac **e** opcionalmente espelhar NVS só para portal offline; Agent-ui deixa de “só TOKEN USB” como caminho único de sync de API  
- Intervalo: respeitar `sync_minutes` do widget (já no painel) — agentd lê do último settings push ou default 15

## Firmware

1. **Quando bridge agent ok + Todoist linked:** não enfileirar `Kind::Tasks` / `Kind::Close` no `todoist_client` (zero TLS Todoist)  
2. Parser `todoist::apply_line` → `task_store` (como `calendar::apply_line`)  
3. Tile/resumo inalterados (já leem `task_store`)  
4. Fallback Wi‑Fi: **só** se cabo ausente **e** token ainda na NVS (compat); log `todoist via wi-fi (sem agent)` — candidato a remover depois  
5. Remover/encolher body 160KB + client TLS keep-alive quando path agent for default  

## O que **não** fazer neste plano

- Aumentar buffers PSRAM / baixar `kTlsMinLargest`  
- SD como estratégia principal de sync  
- Proxy genérico HTTP no agentd  
- Mudar UI do widget Todoist além do necessário para stale/bridge

## Migração

1. Spec + crate + wire encode/decode + testes golden  
2. agentd usb_loop push/query/close  
3. Firmware apply_line + gate “agent preferred”  
4. Agent-ui / portal: token → Mac; Doctor: “Todoist no Mac”  
5. Cutover: `make agent-install` + flash painel; validar logs **sem** `todoist TLS`  
6. (Opcional) apagar path TLS do painel se fallback não for mais necessário  

## Critério de pronto

- [x] Com cabo + token no Mac: listas e close via USB (`todoist_via_agent` — sem `Kind::Tasks` TLS)  
- [x] Sem cabo: último `task_store` permanece; fallback Wi‑Fi só se agent ausente + token NVS (documentado em `todoist-agent.md`)  
- [x] Token não aparece em Activity/logs (`TODOIST_TOKEN` → `token (recebido)`; teste em `logutil`)  
- [x] Suíte: native `test_todoist_bridge_*` + `cargo test -p tessera-caps-todoist` + `cargo test -p agentd` (logutil) + `make e2e`  
- [x] Spec de capabilities + guia `docs/guides/todoist-agent.md`  

**Ainda opcional (não bloqueia aceite):** SD cold-boot; remover path TLS do painel de vez.

## Por que não SD-first

SD no painel seria cache frio útil **depois** do agent. Sozinho: ainda exige TLS ocasional no ESP (DRAM). Agent-first: Mac é o “PSRAM infinito + TLS barato”; painel fica só paint + toque.
