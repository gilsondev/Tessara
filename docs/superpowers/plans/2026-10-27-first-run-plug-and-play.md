# Primeira instalação plug-and-play — plano

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Um notebook novo liga o acesso aos arquivos, a cota das IAs e a agenda Google com o assistente guiando cada gesto.

**Architecture:** O LaunchAgent roda `Tessera Agent.app`. O doctor aponta o Finder para esse app. A varredura grava os slots do painel. O OAuth da agenda é loopback no agente. O Claude Code ganha a linha de status se ainda não tiver uma.

**Tech Stack:** Rust (agentd, doctor, scan, caps-calendar), Tauri, HTML/JS do agent-ui, bash do instalador.

## Global Constraints

- Disco completo não tem API de concessão. O usuário liga o interruptor.
- Não gravar segredo OAuth inventado no repositório.
- Não substituir um `statusLine` que o usuário já configurou, salvo se o comando já for o script do Tessera.
- Texto do painel para espera cabe em 27 caracteres.
- Responder e escrever a UI em português.

---

### Task 1: App do agente

**Files:**
- Modify: `site/public/install.command`
- Modify: `apps/agent-ui/src-tauri/src/service.rs`
- Modify: `agentd/crates/doctor/src/lib.rs`

- [x] Empacotar o binário em `Tessera Agent.app`, assinar ad hoc e apontar o plist para `Contents/MacOS/Tessera`.
- [x] `agentd_identity_path` prefere esse app. O hint pede para arrastar «Tessera Agent».
- [x] Teste: home temporário com o bundle devolve o caminho do app.

### Task 2: Um cartão de permissão e a varredura

**Files:**
- Modify: `apps/agent-ui/src/main.js`
- Modify: `apps/agent-ui/src/index.html`
- Modify: `agentd/crates/scan/src/lib.rs`
- Modify: `agentd/crates/caps-aitokens/src/lib.rs`

- [x] A tela de autorizações mostra só o cartão de Disco completo.
- [x] Agenda sem conta fica “Conta Google ainda não ligada”. Claude pede abrir o Code. A varredura grava `show_aitokens_*`.
- [x] Sem número, o fio manda `abra Code`, `acesso` ou `login`.

### Task 3: Linha de status do Claude Code

**Files:**
- Create: `agentd/crates/agentd/src/claude_hook.rs`
- Modify: `agentd/crates/agentd/src/main.rs`

- [x] Gravar `~/.tessera/agentd/statusline_claude.sh`.
- [x] Preencher `statusLine` em `~/.claude/settings.json` só quando estiver vazio ou já for o script do Tessera.

### Task 4: Conta Google no agente

**Files:**
- Create: `agentd/crates/caps-calendar/src/auth.rs`
- Modify: `agentd/crates/caps-calendar/src/lib.rs`
- Modify: `agentd/crates/caps-calendar/Cargo.toml`
- Modify: `agentd/crates/agentd/src/ipc.rs`
- Modify: `apps/agent-ui/src/ipc.js`
- Modify: `apps/agent-ui/src/main.js`
- Modify: `apps/agent-ui/src/index.html`

- [x] Importar o JSON Desktop, abrir o browser, voltar em `127.0.0.1`, gravar a conta e o catálogo.
- [x] IPC `/v1/calendar/client`, `/v1/calendar/auth/start`, `/v1/calendar/auth`, `/v1/calendar/selection`.
- [x] O assistente e Widgets usam esses caminhos. Sem JSON, o texto explica o cliente Desktop e o seletor de arquivo.
