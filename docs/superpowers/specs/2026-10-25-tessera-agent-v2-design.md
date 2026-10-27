# Tessera Agent v2 — redesign (daemon + companion UI)

**Status:** cutover concluído (2026-10-26) — produto = `agentd` + Tessera.app; `agent/` Python removido  
**Data:** 2026-10-25 (revisão 2026-10-26)  
**Relacionado:**  
- `2026-10-24-tessera-agent-design.md` (MVP Python histórico)  
- `2026-10-24-tessera-agent-capabilities-design.md` (mapa MEDIA / FOCUS / DEV)  
- `docs/prototypes/agent-ui/` (UI companion)  
- `2026-10-25-tessera-site-branding-design.md` (Pages + brand + release CI)  
**Substituiu:** runtime Python em `agent/` / `com.tessera.agent`. Protocolo USB no firmware permanece.

## Decisão de produto

| Pergunta | Resposta |
|---|---|
| Reescrever ou evoluir o Python? | **Reescrever** ao lado; Python fica estável até cutover |
| Quando descartar o atual? | Só com **paridade USB**, UI usável, Doctor verde e testes de regressão no Mac real |
| Stack | **agentd (Rust)** + **companion UI (Tauri)** + protocolo USB **igual** ao de hoje |
| Tooling | **mise** no root: versões (Rust/Node/Python) + tasks (`agentd-*`, `agent-ui-*`, `agent-legacy-*`) |
| Multi-notebook | Perfil de **config** por Mac em `~/.tessera/`; cabo = ativo; SD **não** guarda credenciais |
| Idioma UI | Português claro; log avançado técnico opcional |

## Visão

O Agent v2 é o companion de mesa do Tessera no notebook:

1. **agentd** — daemon em segundo plano: USB, capabilities, watchers, self-update, Doctor.  
2. **App Tessera** — janela + menu (tray): onboarding, varredura, PCs, Painel, Atividade, Doctor.  
3. **Painel JC3248** — continua consumindo as mesmas linhas USB (`AGENT`, `AITOKENS`, `MEDIA`, `FOCUS`, `CALENDAR`, …).

O companion de dia a dia é **agentd** (`com.tessera.agentd` + Tessera.app).

## Objetivos

- Paridade de protocolo com o agent 0.3.x (não exigir firmware novo só para o cutover).  
- UX do protótipo: varredura, multi-PC (perfis), Doctor, Atividade simples/avançada, controle do painel com cabo.  
- Instalação plug-and-play: LaunchAgent + app; permissões pedidas só do que a varredura marcou.  
- Toolchain reproduzível via mise (CI e máquina local).  
- Cutover limpo: um binário/serviço; remover Python do path de produto.

## Não-objetivos (v2 cutover)

- Reescrever firmware / mudar wire format sem necessidade.  
- LLM no Mac acionado pelo painel.  
- Shell arbitrário / abrir apps genéricos.  
- Guardar tokens/OAuth/TCC no SD do painel.  
- Linux/Windows no 1º cutover (desenhar portável; ship macOS arm64/x86_64 primeiro).

## Arquitetura

```
┌─────────────────────────────┐         USB CDC          ┌──────────────┐
│  App Tessera (Tauri)        │◄── localhost IPC ───────►│   agentd     │
│  tray · janela · Doctor UI  │     (sock / HTTP local)  │  Rust daemon │
└─────────────────────────────┘                          │  capabilities│
                                                         └──────┬───────┘
                                                                │
                                                         JC3248 │
                                                         firmware
                                                                │
                                                         SD: agent pack
                                                         (instalador v2)
```

**Abordagem B (capabilities):** um prefixo por domínio — igual ao mapa atual. Novas features = novo módulo, não envelope monolítico.

### Processos no Mac

| Processo | Papel |
|---|---|
| `agentd` | Único dono da porta USB; push/poll; persistência de perfil; Doctor engine |
| `Tessera.app` | UI; não fala USB direto; pergunta ao agentd |
| LaunchAgent `com.tessera.agentd` | Sobe agentd no login (substitui `com.tessera.agent` no cutover) |

Durante coexistência: labels e paths **diferentes** (`com.tessera.agentd` + `~/.tessera/agentd/`) para não brigar com o Python.

### Árvore no monorepo (nova)

```
mise.toml                 # versões + tasks
agent/                    # LEGACY Python — freeze (só hotfix crítico)
agentd/                   # NEW
  Cargo.toml              # workspace
  crates/
    protocol/             # parse/encode linhas USB (testes golden vs Python)
    usb/                  # serial CDC + DTR
    profile/              # machine_id, config, scan result
    doctor/               # checks → report JSON
    caps-aitokens/
    caps-media/
    caps-focus/
    caps-calendar/
    agentd/               # binário daemon + IPC
apps/
  agent-ui/               # Tauri 2 + UI (base: docs/prototypes/agent-ui)
```

### Perfis multi-notebook (config, não acesso)

Verdade no Mac:

```
~/.tessera/
  machine.json          # machine_id estável, display_name (“Mac do trabalho”)
  agentd/
    config.toml         # toggles: media, focus, aitokens + aitokens_{cursor,claude,codex}, calendar
    scan.json           # último resultado da varredura (providers + enabled)
    state/              # caches não-secretos (ex.: último Focus ok)
```

- **Primeira vez** naquele Mac → varredura + autorizações + cabo.  
- **Próximas** → handshake; pula wizard.  
- **Outro Mac** → outro `~/.tessera/` (outro perfil).  
- Painel / SD: no máximo roster leve (`machine_id`, apelido, `last_seen`) — **sem** tokens.

### SD / install

- Continua: pacote no SD servido pelo portal (`/agent/…`).  
- v2 pack: binários `agentd` + app (ou `.pkg` / `.dmg` gerado no CI) em vez de (ou além de) tarball Python.  
- Durante coexistência: path/versão distintos (`agent.version` vs `agentd.version`) até o cutover unificar.

## Protocolo USB (contrato de paridade)

Fonte de verdade do comportamento atual: `agent/src/agent/` + testes nativos de snapshot no firmware.

Linhas mínimas que o agentd **deve** emitir/responder igual ao Python antes do cutover:

| Prefixo | Direção | Notas |
|---|---|---|
| `AGENT\tv1\t…` | → painel | handshake / ok |
| `AITOKENS` / `AITOKENS?` | ↔ | cotas |
| `MEDIA` / `MEDIA?` / `MEDIA_COVER` | ↔ | now playing + capa |
| `FOCUS` / `FOCUS?` / `SET` / `OFF` | ↔ | lista + ativo |
| `CALENDAR*` | ↔ | como hoje |
| `AGENT_PUSH` (arm) | painel→ | inalterado no firmware |

Regra: **golden tests** — fixtures de linhas capturadas do Python; agentd deve produzir wire equivalente (campos na mesma ordem).

## Companion UI (escopo cutover)

Base: `apps/agent-ui/` (antes `docs/prototypes/agent-ui/`).

| Superfície | Papel |
|---|---|
| Welcome → Varredura → Achados → Autorizar → Cabo | Onboarding 1ª vez |
| Início | Mac ativo + atalhos Doctor / Painel |
| Painel | Controles do aparelho (Wi‑Fi `tessera.local`) + board **Na home** (ocultar/exibir widgets). USB não grava dashboard |
| Widgets | Só widgets **ativos** na home; configs por widget (data com preview, Todoist token, clima cidade, AI Tokens ordem/fontes, Agenda contas). Música/Focus: status + link No Mac — sem fake settings |
| No Mac | Coleta neste Mac (Música, Focus, Agenda, cotas Cursor/Claude/Codex). Cotas ↔ `show_aitokens_*` no painel (sync bidirecional) |
| PCs | Perfis do **companion** neste/outros Macs (não inventário do painel). Cabo USB = quem manda agora |
| Atividade | **Resumo** = feed amigável do log USB/painel (não clona Doctor). **Log** = linhas técnicas. Logs agentd humanizam valores (`estado=…`, `17:00 · título`) |
| Doctor | Checklist saúde → CTAs |
| Mais | **Atualizar sozinho** (self-update Auto — ver ADR `0003`), remover deste Mac |

**Hide sticky:** `ensure_media_mac` / `ensure_calendar` no firmware são **one-shot** (`media_mac_restored` / `cal_restored` na NVS). Ocultar no portal/Agente deve persistir após reboot.

**Self-update:** poll `https://gilsondev.github.io/Tessara/latest.json`; com toggle Auto ligado, baixa Release (agentd + Tessera.app), verifica SHA-256 e reinstala sem prompt. Spec site: `2026-10-25-tessera-site-branding-design.md`.

## Doctor

Engine no agentd; UI só renderiza.

Checks (mínimo cutover):

1. LaunchAgent / processo agentd rodando  
2. Porta USB / handshake `AGENT`  
3. FDA / arquivos (Focus, Claude paths)  
4. MEDIA watcher ok  
5. FOCUS ok ou degradado  
6. Cada fonte AITOKENS (Cursor / Claude / Codex): ok / parcial / N/A  
7. Fila de push (sem backlog infinito)

Saída: JSON estável para UI + “Copiar relatório” (texto).

## Roadmap de capabilities (pós-paridade)

Ordem sugerida **depois** do cutover estável. Cada item = spec addendum curto + plano próprio.

| Fase | Capability | Consulta / ação | Dependência |
|---|---|---|---|
| **P0** | Paridade v1 | Tudo que o Python faz hoje | Cutover |
| **P1** | Dev desk | Agente “quente”, fonte offline (além do %) | AITOKENS |
| **P2** | Bateria | %, charging, ETA se barato | `SYS` ou `POWER` |
| **P3** | Áudio sistema | volume, mute, output name | permissões leves |
| **P4** | Rede desk | Wi‑Fi up, VPN on/off (sem SSID obrigatório) | — |
| **P5** | Display | external display connected | — |
| **P6** | Git desk | branch do cwd “do momento” (opt-in) | só se sem ruído |
| **P7** | CI badge | 1 status, não feed | rede no Mac |
| — | Sleep / shell / LLM | Fora / hold + confirmação | — |

Calendário e mídia/Focus já contam como P0 se estiverem na paridade.

## Tooling — mise

Arquivo root `mise.toml`:

- **Tools:** `rust`, `node` (UI), `python` (legacy + scripts até cutover), opcional `pnpm`/`cargo-binstall`.  
- **Tasks:** `agentd-build`, `agentd-test`, `agentd-run`, `agent-ui-dev`, `agent-ui-build`, `agent-install` (build + cutover local), `agent-legacy-run`, `agent-pack-v2`, `doctor-smoke`.  
- Makefile: `make agent-install` / `pack-agentd` delegam a mise/scripts sem depender de agente de IA.

Bootstrap: `mise trust` + `mise install` documentado em `docs/getting-started.md` (addendum no plano mise).

## Critérios de cutover (gate) — fechados

1. `mise run agentd-test` + golden de protocolo — OK.  
2. Mac real: MEDIA, FOCUS, AITOKENS, CALENDAR / TODOIST via agentd — OK.  
3. App: onboarding + Doctor FDA — OK.  
4. `tools/cutover_agent_v2.sh` / `make agent-install` — path padrão.  
5. Pack SD / portal: `make pack-agentd` → `/tessera/agentd.tgz` + `PUT/GET /agentd/…`.  
6. **`agent/` Python removido do tronco** (2026-10-26).

Checklist: `tools/agent_v2_gate.md`.

## Riscos

| Risco | Mitigação |
|---|---|
| Drift de protocolo | Golden fixtures do Python; CI compara |
| Duas versões no Mac | Paths/labels distintos; Doctor avisa se ambos ativos |
| APIs macOS frágeis (Focus/Media) | Isolar crates; degradar `ok=0` |
| Escopo UI infinito | Cutover = protótipo P0; polish depois |
| mise vs Makefile | mise = versões+tasks agent; Make = finish-gate firmware |

## Planos de implementação

| Plano | Entrega |
|---|---|
| `plans/2026-10-25-agent-v2-mise.md` | `mise.toml`, tasks, docs bootstrap |
| `plans/2026-10-25-agent-v2-daemon.md` | Workspace Rust, USB, `AGENT`, IPC, golden tests |
| `plans/2026-10-25-agent-v2-ui.md` | Tauri app a partir do protótipo |
| `plans/2026-10-25-agent-v2-parity-cutover.md` | Caps parity, perfis, Doctor, pack SD, cutover, archive Python |

## Glossário (addendum)

- **agentd** — daemon Rust do Agent v2.  
- **Agent legacy** — processo Python `com.tessera.agent` (removido; cutover 2026-10-26).  
- **Perfil de máquina** — config local em `~/.tessera/` (não credenciais).  
- **Doctor** — checagem de saúde do companion + CTAs.
