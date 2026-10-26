# Agent v2 — mise + toolchain Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Centralizar versões de Rust/Node/Python e tasks do Agent v2 (e legacy) no `mise.toml` do monorepo, sem quebrar o finish-gate do firmware.

**Architecture:** mise no root é a fonte de versões; tasks `agentd-*` / `agent-ui-*` / `agent-legacy-*` encapsulam comandos. Makefile pode chamar `mise run` depois; este plano só introduz mise + docs.

**Tech Stack:** mise 2026.x, Rust (stable pinned), Node 22 LTS, Python 3.12 (legacy agent + scripts).

## Global Constraints

- Spec: `docs/superpowers/specs/2026-10-25-tessera-agent-v2-design.md`
- Não remover `agent/` Python nem alterar launchd de produção neste plano
- Não exigir mise para `make test` / `make e2e` firmware ainda (opt-in; documentar)
- Idioma de docs de usuário: português; nomes de tasks em inglês kebab-case

---

### Task 1: `mise.toml` com tools pinados

**Files:**
- Create: `mise.toml`
- Create: `.mise.toml` **não** — um único `mise.toml` na root
- Modify: `.gitignore` (se necessário: ignorar `mise.local.toml`)

**Interfaces:**
- Produz: tools `rust`, `node`, `python` instaláveis via `mise install`

- [ ] **Step 1: Criar `mise.toml`**

```toml
# Tessera — toolchain (Agent v2 + legacy Python + UI)
# Docs: docs/superpowers/specs/2026-10-25-tessera-agent-v2-design.md

[tools]
rust = "1.83.0"
node = "22.14.0"
python = "3.12.9"

[settings]
# Monorepo: tasks na root; crates em agentd/ depois
experimental = true

[env]
_.file = ".env"
# PYTHONPATH só em tasks legacy
```

- [ ] **Step 2: Garantir trust + install**

Run:

```bash
cd /Users/gilson/Developer/Projects/gilsondev/Tessera
mise trust
mise install
mise which rustc
mise which node
mise which python
```

Expected: paths sob `~/.local/share/mise/...` (ou equivalente) e versões compatíveis com o pin.

- [ ] **Step 3: Commit**

```bash
git add mise.toml
git commit -m "$(cat <<'EOF'
chore(mise): pin rust, node, python for agent v2

EOF
)"
```

---

### Task 2: Tasks mise (placeholders até crates existirem)

**Files:**
- Modify: `mise.toml`
- Create: `scripts/mise/agent-legacy-run.sh`
- Create: `scripts/mise/check-agent-trees.sh`

**Interfaces:**
- Consome: árvore `agent/` existente
- Produz: `mise run agent-legacy-run`, `mise run agent-doctor-smoke` (smoke no-op até agentd), `mise run agent-check-trees`

- [ ] **Step 1: Script de sanidade das árvores**

`scripts/mise/check-agent-trees.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
test -d "$ROOT/agent/src/agent" || { echo "missing legacy agent/"; exit 1; }
echo "legacy agent/: ok"
if [[ -d "$ROOT/agentd" ]]; then
  echo "agentd/: present"
else
  echo "agentd/: not created yet (expected until daemon plan)"
fi
if [[ -d "$ROOT/apps/agent-ui" ]]; then
  echo "apps/agent-ui/: present"
else
  echo "apps/agent-ui/: not created yet (expected until ui plan)"
fi
```

- [ ] **Step 2: Wrapper legacy run**

`scripts/mise/agent-legacy-run.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT/agent"
if [[ ! -d .venv ]]; then
  python -m venv .venv
  .venv/bin/pip install -r requirements.txt
fi
export PYTHONPATH=src
exec .venv/bin/python -m agent "$@"
```

- [ ] **Step 3: Tasks no `mise.toml`**

Append:

```toml
[tasks.agent-check-trees]
description = "Sanity: legacy/agentd/ui trees"
run = "bash scripts/mise/check-agent-trees.sh"

[tasks.agent-legacy-run]
description = "Run Python agent (legacy) from agent/"
run = "bash scripts/mise/agent-legacy-run.sh"

[tasks.agentd-build]
description = "Build agentd (Rust) — available after daemon plan"
dir = "agentd"
run = "cargo build --release"
# hide until crate exists: use `mise run` only after agentd/ exists

[tasks.agentd-test]
description = "Test agentd workspace"
dir = "agentd"
run = "cargo test"

[tasks.agentd-run]
description = "Run agentd in foreground"
dir = "agentd"
run = "cargo run -p agentd"

[tasks.agent-ui-dev]
description = "Tauri/Vite dev — after ui plan"
dir = "apps/agent-ui"
run = "npm run tauri dev"

[tasks.agent-ui-build]
description = "Build Tessera.app"
dir = "apps/agent-ui"
run = "npm run tauri build"

[tasks.doctor-smoke]
description = "Placeholder until doctor crate; exits 0 if trees ok"
run = "bash scripts/mise/check-agent-trees.sh"
```

- [ ] **Step 4: chmod + smoke**

```bash
chmod +x scripts/mise/*.sh
mise run agent-check-trees
```

Expected: `legacy agent/: ok` e mensagens “not created yet” para agentd/ui.

- [ ] **Step 5: Commit**

```bash
git add mise.toml scripts/mise/
git commit -m "$(cat <<'EOF'
chore(mise): add agent legacy/v2 task stubs

EOF
)"
```

---

### Task 3: Documentação bootstrap

**Files:**
- Modify: `docs/getting-started.md` (seção curta Agent / mise)
- Modify: `agent/README.md` (ponte “legado”; link v2 spec)
- Create: `docs/guides/agent-v2-dev.md`

**Interfaces:**
- Produz: guia único para `mise install` + tasks

- [ ] **Step 1: Criar `docs/guides/agent-v2-dev.md`**

Conteúdo mínimo:

```markdown
# Agent v2 — desenvolvimento

Spec: `docs/superpowers/specs/2026-10-25-tessera-agent-v2-design.md`

## Toolchain

```bash
mise trust
mise install
mise run agent-check-trees
```

## Legacy (Python, produção atual)

```bash
mise run agent-legacy-run
```

## v2 (quando as árvores existirem)

```bash
mise run agentd-test
mise run agentd-run
mise run agent-ui-dev
```

Finish-gate do **firmware** continua em `make test` / `make e2e` (PlatformIO).
```

- [ ] **Step 2: Em `docs/getting-started.md`, adicionar bullet**

Sob ferramentas / agent:

```markdown
- Agent v2 (em construção): [guides/agent-v2-dev.md](guides/agent-v2-dev.md) — `mise install`
- Agent legacy (Python): `cd agent && …` ou `mise run agent-legacy-run`
```

- [ ] **Step 3: Em `agent/README.md`, no topo após o título**

```markdown
> **Legado.** O produto atual é este pacote Python. O redesign (Rust agentd + Tauri) está em
> `docs/superpowers/specs/2026-10-25-tessera-agent-v2-design.md`. Até o cutover, hotfix aqui; features novas no v2.
```

- [ ] **Step 4: Commit**

```bash
git add docs/guides/agent-v2-dev.md docs/getting-started.md agent/README.md
git commit -m "$(cat <<'EOF'
docs: agent v2 mise bootstrap and legacy freeze note

EOF
)"
```

---

## Spec coverage

| Spec | Task |
|---|---|
| mise versões + tasks | 1–2 |
| Docs bootstrap | 3 |
| Não quebrar finish-gate | Tasks agentd não rodam no Make ainda |

## Handoff

Plano mise completo → seguir `2026-10-25-agent-v2-daemon.md`.
