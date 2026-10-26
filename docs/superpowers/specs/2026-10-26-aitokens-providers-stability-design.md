# AI Tokens — estabilidade de providers + tile transparente + config no Agente

**Status:** approved (owner 2026-10-26)  
**Data:** 2026-10-26  
**Relacionado:** `2026-10-24-ai-tokens-design.md`, `2026-10-25-tessera-agent-v2-design.md`, `docs/prototypes/agent-ui/README.md`, `apps/agent-ui/IPC.md`

## Problemas observados

1. **Cursor some do tile** sem o usuário desligar a fonte.  
2. **Home soma** `autoPercentUsed + apiPercentUsed` (ex. `3% · 13%` → `16% usados`), o que passa de 100% e confunde.  
3. Em **Mac novo / já configurado**, falta um fluxo claro de **varredura → escolher o que monitorar** alinhado ao que cada fornecedor realmente expõe.  
4. O **portal web** já configura widgets (ex. toggles AI Tokens); o **Agente (agent-ui)** ainda não oferece a mesma superfície para o dia a dia no Mac.

## Diagnóstico (código atual)

| Camada | Comportamento que causa o “sumiço” |
|---|---|
| `caps-aitokens` / `cursor_live::fetch_usage` | Qualquer falha (401/refresh, `planUsage` incompleto, API mudou) → `available=0` no slot `cursor` |
| Home `set_aitokens` | Se `!src->available` **esconde a linha inteira** (`LV_OBJ_FLAG_HIDDEN`) — parece que o provider sumiu |
| Detalhe `aitokens_page` | Idem: `source_ok` falso → bloco invisível |
| Cache agentd | TTL do `AITOKENS` line (~intervalo de refresh) guarda também o estado `unavailable` — falha transitória “gruda” até o próximo ciclo bom |
| Home Cursor | `sscanf` no `display` e **soma** `a + b` — legado; detalhe já mostra Modelos/Outros separados |

Credenciais podem existir (state.vscdb / Keychain / auth.json) e mesmo assim o tile some se a chamada live falhar uma vez.

## Objetivos

1. **Não sumir** Cursor (nem Codex/Claude) só porque a cotas live falhou uma vez.  
2. **Tile transparente** para Cursor: duas porcentagens (Modelos / Outros), **sem soma**.  
3. **Varredura no Agente** detecta providers instalados/logados e deixa o usuário escolher o que empurrar ao painel — no setup e em Macs novos; re-varrer em Macs já configurados.  
4. **Config de widgets no Agente** (começando por AI Tokens) no mesmo espírito do portal web.

## Decisões de produto

### A. Visibilidade no painel (anti-sumiço) + cache de últimos valores

Enquanto Cursor/Codex/Claude “somem” por falha temporária (API, token, rede), **não deixar o tile/detalhe vazio**: continuar mostrando os **últimos valores bons**. Só limpar esse cache se passar **várias horas** sem nenhuma cotas live bem-sucedida daquela fonte.

| Estado | Tile / detalhe |
|---|---|
| Live ok | Mostra métricas atuais; atualiza cache last-good |
| Live falhou, last-good **ainda válido** (&lt; TTL longo) | Empurra / pinta **os mesmos números** + `· desatualizado` (firmware já marca stale cedo ~3 min sem push; isso é rótulo, não limpeza) |
| Live falhou e last-good **expirou** (várias horas sem sucesso) | Pode limpar números; linha/bloco permanece **visível** se a fonte está habilitada + credencial/scan diz presente → texto curto (`offline` / `sem cotas`) — **não** ocultar |
| Live falhou e **nunca** houve last-good | Linha visível com estado curto — **não** ocultar |
| Fonte desabilitada pelo usuário | Oculta (toggle) |

Regra: **ocultar ≠ indisponível**. Ocultar só por preferência do usuário. Cache vazio ≠ esconder a linha.

#### Cache last-good (agentd)

| Item | Decisão |
|---|---|
| O quê | Por fonte (`cursor` / `claude` / `codex`): último `AiTokenSource` com `available=1` |
| Quando usa | Qualquer poll live que devolver `available=0` (ou omitir) enquanto o cache for válido |
| TTL de **retenção** | **6 horas** sem refresh bem-sucedido daquela fonte → descartar last-good (`AITOKENS_LAST_GOOD_TTL_S = 21600`). “Várias horas” — constante documentada; ajustável depois |
| Persistência | Preferir `~/.tessera/agentd/state/aitokens_last_good.json` (sobrevive a restart do agentd). Fallback aceitável no MVP: só RAM do processo + mesmo TTL |
| Wire | Continua `available=1` + `display`/`value` antigos enquanto servir cache; painel trata age/stale como hoje. Após TTL: `available=0` (ou display de status) sem inventar % |

Firmware: se a linha ainda trouxer valores (cache agentd), pintar normalmente + `desatualizado` quando `refresh_age` pedir. Não zerar UI só porque um poll intermediário falhou.

### B. Cursor no tile — sem soma

**Supersede** a linha da spec antiga (“Cursor: soma dos % usados”).

| Superfície | Formato |
|---|---|
| Home (uma linha) | `Cursor · Modelos N% · Outros M%` (+ reset / alerta se couber) |
| Detalhe | Já: barras **Modelos** / **Outros** (% usados) — manter |
| Alertas | Continuam no **pior restante**: `100 − max(Modelos, Outros)` (não usar soma) |

Não inventar um único % agregado. Não usar `totalPercentUsed` da API Cursor.

### C. Varredura + preferências de provider

Já existe `POST /v1/scan` + `scan.json` detectando `cursor` / `claude` / `codex`. Evoluir:

| Peça | Comportamento |
|---|---|
| Scan | Detecta presença (auth / statusLine / paths) — **não** exige cotas live ok |
| Achados | Por provider: `missing` \| `ready` \| `needs_auth` + toggle **monitorar** |
| Persistência agentd | Preferências **por fonte** em `~/.tessera/agentd/config.toml` (ex. `aitokens.cursor`, `aitokens.claude`, `aitokens.codex`) — hoje só `aitokens: bool` global |
| Push USB | Só inclui fontes com toggle local ligado **e** (opcional) ainda com slot `available`/stale conforme §A |
| Painel | Continua com `show_aitokens_*` (o que desenhar na home). Default: espelhar o que o Agente está monitorando quando houver cabo; usuário pode refinar no portal **ou** no Agente |

**Mac novo / 1ª vez:** Welcome → Varredura → Achados (toggles) → autorizar → cabo.  
**Mac já configurado:** “Varrer de novo” atualiza achados sem apagar toggles do usuário (só sugere ligar fontes novas detectadas).

Cada fornecedor só oferece o que a API/arquivo local mostra (Codex 5h+semana, Claude %, Cursor Modelos+Outros) — a UI não inventa métricas.

### D. Config de widgets no Agente

O portal web já tem páginas por widget (AI Tokens, Agenda, Todoist…). No **agent-ui**:

| Escopo MVP desta spec | Depois |
|---|---|
| Aba **Widgets → AI Tokens**: toggles por fonte (sem copy “na home”) + **ordem dos cards** (`sources_order`) + status | “Pedir cotas” / re-varrer se útil |
| Aba **No Mac**: toggles de coleta Cursor/Claude/Codex | Sync **bidirecional** com `show_aitokens_*` no aparelho |

Princípio: **o que o portal configura no aparelho**, o Agente pode configurar quando o Mac está ativo (USB ou Wi‑Fi `tessera.local`), sem forçar o usuário a abrir o celular.

**Sync cotas:** ligar/desligar em No Mac grava scan **e** `POST /v1/panel/settings` (`show_aitokens_*`). Ligar/desligar em Widgets → AI Tokens atualiza painel **e** o toggle No Mac / `config.toml`.

IPC: reutilizar `/v1/panel/settings` + `/api/widget_settings` (proxy Agente) onde já existir; para preferências **só-Mac** (quais fontes coletar), usar config.toml / rotas de scan — não misturar com NVS do aparelho sem cabo.

**Ordem:** widget setting `sources_order` (CSV `codex,claude,cursor` …) no portal e no Agente — chips “Ordem dos cards”.

**Altura do tile:** widgets com `supports_home_tile_size` devem ter chips S–XL no portal **e** no Agente — `2026-10-26-home-tile-size-calendar-cache-design.md` + plano `2026-10-26-home-tile-size-web-and-agent.md`.

## Protocolo USB

Contrato `AITOKENS\tv1\t…` **inalterado** no wire.  
`display` do Cursor permanece `N% · M%` (Modelos · Outros). A home **para de somar** e passa a rotular.

Opcional futuro (fora deste MVP): `kind=dual_pct` ou segundo value — só se o parser atual ficar apertado.

## Fora desta spec

- Novos providers além de Codex / Claude / Cursor.  
- Histórico / gráficos.  
- Persistir snapshot de cotas em flash do aparelho.  
- Mudar as APIs Cursor/Codex além do necessário para estabilidade.

## Critérios de aceite

1. Com Cursor (ou outro provider) já com cotas ok, falha transitória **mantém os últimos % no tile/detalhe** (com desatualizado); só limpa números após ~6 h sem sucesso; a linha não some por toggle sozinho.  
2. Home Cursor mostra **duas** % (Modelos / Outros), nunca soma > 100% por agregação.  
3. Scan em Mac limpo lista providers presentes; toggles persistem; push respeita toggles.  
4. agent-ui permite ligar/desligar fontes AI Tokens (e preferência de exibição no painel quando conectado) sem abrir o portal no celular.  
5. Altura de tile (`home_tile_size`) configurável no portal **e** no Agente para widgets que optarem (Calendário no mínimo).  
6. `make test` + testes Rust `caps-aitokens` / `scan` verdes; e2e host sem regressão do tile.
