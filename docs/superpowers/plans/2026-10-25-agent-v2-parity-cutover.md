# Agent v2 — capability parity, multi-Mac, Doctor, cutover Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Paridade USB com o agent Python 0.3.x, perfis multi-notebook, Doctor real, pack SD v2 e cutover que descarta o legacy só após gate verde.

**Architecture:** Crates `caps-*` no workspace `agentd/` portam a lógica de `agent/src/agent/capabilities/` (comportamento, não line-by-line). UI já consome IPC. Script `tools/cutover_agent_v2.sh` migra LaunchAgent. Roadmap P1+ **não** entra neste plano (só listado no spec).

**Tech Stack:** Rust agentd, scripts bash, LaunchAgent plist, pack no SD (`agentd` artifacts).

## Global Constraints

- Spec: `docs/superpowers/specs/2026-10-25-tessera-agent-v2-design.md`
- Pré-req: mise + daemon (USB/IPC/profile) + UI scaffold
- **Não** deletar `agent/` até Task 7 (gate)
- Firmware: zero mudança obrigatória; se precisar, PR separada
- Segredos: nunca no SD; OAuth Calendar continua no Mac

---

### Task 1: Capability MEDIA (paridade)

**Files:**
- Create: `agentd/crates/caps-media/`
- Modify: `agentd/crates/agentd` (register push + `MEDIA?`)
- Fixtures: `agentd/crates/protocol/tests/fixtures/lines/media_*.txt`
- Reference: `agent/src/agent/capabilities/media/`

**Interfaces:**
- Produz: `encode_media_line(info) -> String` golden-compatible
- Push on dirty + heartbeat; `MEDIA_COVER` não bloqueia metadado

- [ ] **Step 1: Inventariar wire real**

```bash
rg -n "MEDIA\\\\tv|def encode|build_line" agent/src/agent/capabilities/media -g'*.py'
```

Documentar campos em `caps-media/README.md` (ordem exata).

- [ ] **Step 2: Testes golden com fixtures capturados do Python**

- [ ] **Step 3: Implementar encoder + stub watcher (retorna stopped) até ObjC/DNC**

Primeira entrega: watcher mínimo (AppleScript ou notificação) **ou** FFI chamando helper; se DNC for complexo, portar script auxiliar em `agentd/helpers/` temporariamente — preferir Rust puro quando viável.

- [ ] **Step 4: Integração no loop USB — só se `config.media`**

- [ ] **Step 5: Teste no Mac com painel: tile Música atualiza**

- [ ] **Step 6: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(agentd): MEDIA capability parity with golden lines

EOF
)"
```

---

### Task 2: Capability FOCUS (paridade)

**Files:**
- Create: `agentd/crates/caps-focus/`
- Reference: `agent/src/agent/capabilities/focus/`
- Doctor hook: FDA missing → check `bad`

**Interfaces:**
- `FOCUS` push, `FOCUS?`, `FOCUS\tSET\t`, `FOCUS\tOFF`
- Sem FDA: `ok=0` (não crash)

- [ ] **Step 1: Golden fixtures + encoder**

- [ ] **Step 2: Ler DB Focus / fallback documentado como no Python**

- [ ] **Step 3: SET/OFF via `shortcuts` CLI (mesmo contrato de nomes)**

- [ ] **Step 4: Device test: tile Mac + detalhe**

- [ ] **Step 5: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(agentd): FOCUS list/SET/OFF parity

EOF
)"
```

---

### Task 3: Capability AITOKENS (paridade)

**Files:**
- Create: `agentd/crates/caps-aitokens/`
- Reference: `agent/src/agent/capabilities/aitokens/`
- Submódulos lógicos: cursor, claude, codex

**Interfaces:**
- Mesmas fontes de credencial/arquivo que o README legacy documenta
- Push periódico + `AITOKENS?`

- [ ] **Step 1: Portar ordem de resolução Cursor (state.vscdb → keychain → auth.json → env)**

- [ ] **Step 2: Claude Desktop/Code merge (% consumido) + cache path sob `~/.tessera/`**

- [ ] **Step 3: Codex**

- [ ] **Step 4: Golden + teste device “Pedir cotas”**

- [ ] **Step 5: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(agentd): AITOKENS parity (Cursor, Claude, Codex)

EOF
)"
```

---

### Task 4: Capability CALENDAR (paridade)

**Files:**
- Create: `agentd/crates/caps-calendar/`
- Reference: `agent/src/agent/capabilities/calendar/`

**Interfaces:**
- OAuth tokens **permanecem** no Mac; portar fluxos `CALENDAR*`, `CALENDAR_AUTH*`, catalog

- [ ] **Step 1: Mapear linhas e estados do Python**

- [ ] **Step 2: Implementar + golden**

- [ ] **Step 3: Teste device agenda tile**

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(agentd): CALENDAR capability parity

EOF
)"
```

---

### Task 5: Scan + multi-Mac profile UX (backend)

**Files:**
- Create: `agentd/crates/scan/`
- Modify: `profile` + IPC `POST /v1/scan`, `GET /v1/scan/last`, `GET /v1/machines`
- Modify: UI já tem telas — ligar fetch

**Interfaces:**
- Scan detecta: apps Cursor/Claude/Codex, Spotify/Music, Focus DB legível?, Calendar linked?
- Persiste `~/.tessera/agentd/scan.json` + atualiza `config.toml` toggles
- `machine.json` display_name editável
- **Não** re-roda wizard se `config.setup_completed = true`

- [ ] **Step 1: API scan retorna lista typed**

```json
{
  "found": [
    { "id": "cursor", "state": "ready|needs_auth|missing", "enabled": true }
  ]
}
```

- [ ] **Step 2: Testes unitários de detecção com paths fake**

- [ ] **Step 3: UI: botões da varredura chamam IPC; “Salvar” grava config**

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(agentd): machine scan and config profile persistence

EOF
)"
```

---

### Task 6: Doctor engine + logs IPC

**Files:**
- Create: `agentd/crates/doctor/`
- Modify: IPC `/v1/doctor`, `/v1/logs`
- Modify: UI remove badge “demo”

**Interfaces:**
- Checks do spec (processo, USB, FDA, media, focus, cada AI, push queue)
- Logs: ring buffer em memória + arquivo `~/.tessera/agentd/agentd.log`

- [ ] **Step 1: `DoctorReport` serde + testes com fake deps**

- [ ] **Step 2: Endpoints**

- [ ] **Step 3: `mise run doctor-smoke` chama `curl -s localhost:19792/v1/doctor | jq .summary`**

Atualizar task mise:

```toml
[tasks.doctor-smoke]
run = "curl -sf http://127.0.0.1:19792/v1/doctor | head -c 200"
```

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(agentd): doctor engine and log IPC

EOF
)"
```

---

### Task 7: Pack SD v2 + cutover scripts + gate

**Files:**
- Create: `agentd/pack_sd.sh` (espelha `agent/pack_sd.sh` mas empacota binários)
- Create: `tools/cutover_agent_v2.sh`
- Create: `tools/agent_v2_gate.md` (checklist manual)
- Modify: `Makefile` targets `push-agentd` (paralelo a `push-agent` até cutover)
- Modify: `GLOSSARY.md` (agentd)
- Optional: move `agent/` → `archive/agent-python/` **somente após gate**

**Interfaces:**
- Cutover: para `com.tessera.agent`, instala `com.tessera.agentd` + copia app para `/Applications/Tessara.app`, marca `setup_completed` se legacy tinha config
- Gate checklist obrigatório antes do delete

- [ ] **Step 1: `pack_sd.sh` gera `dist/sd/tessera/agentd/` com version sidecar**

- [ ] **Step 2: `cutover_agent_v2.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
# 1. brew/mise build already done
# 2. launchctl bootout legacy if loaded
# 3. install agentd LaunchAgent
# 4. open Tessara.app
# 5. curl doctor until summary ok|warn
```

- [ ] **Step 3: Rodar gate manual (documentar resultados em PR)**

Checklist (`tools/agent_v2_gate.md`):

- [ ] MEDIA dirty-push < 1s no painel  
- [ ] FOCUS SET aplica no Mac  
- [ ] AITOKENS três fontes ou N/A correto  
- [ ] CALENDAR eventos  
- [ ] 2ª abertura do app sem wizard  
- [ ] Doctor pede FDA quando revogado  
- [ ] Legacy desligado; só agentd na USB  

- [ ] **Step 4: PR de remoção do Python do path de produto**

Só depois do checklist assinado. Atualizar portal/install.command para v2. `mise` task `agent-legacy-run` pode permanecer apontando para archive por 1 ciclo.

- [ ] **Step 5: Commit cutover tooling (antes do delete)**

```bash
git commit -m "$(cat <<'EOF'
feat(agentd): SD pack v2 and cutover script

EOF
)"
```

- [ ] **Step 6: Commit archive/delete (PR separada)**

```bash
git commit -m "$(cat <<'EOF'
chore: retire Python Tessara agent after v2 cutover gate

EOF
)"
```

---

## Roadmap pós-cutover (não implementar aqui)

Ver tabela P1–P7 no spec. Próximo plano sugerido após estabilidade: **Dev desk (P1) + Bateria (P2)**.

---

## Spec coverage

| Spec | Task |
|---|---|
| MEDIA/FOCUS/AITOKENS/CALENDAR | 1–4 |
| Scan + perfis | 5 |
| Doctor | 6 |
| Cutover / descartar Python | 7 |
| Roadmap P1+ | documentado, fora |

## Self-review notes

- Golden fixtures obrigatórios antes de declarar parity.  
- Dois LaunchAgents nunca no cutover final.  
- SD sem segredos reafirmado no pack script (assert paths).

## Execution handoff

**Plan complete.** Ordem:

1. `2026-10-25-agent-v2-mise.md`  
2. `2026-10-25-agent-v2-daemon.md`  
3. `2026-10-25-agent-v2-ui.md` (paralelo parcial com daemon Task 4+)  
4. Este plano  

**Two execution options:** Subagent-Driven (recommended) ou Inline — diga qual após revisar a spec.
