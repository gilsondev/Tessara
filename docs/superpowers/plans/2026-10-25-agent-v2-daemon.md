# Agent v2 — agentd (daemon Rust) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Criar o workspace Rust `agentd/` com protocolo USB testável, handshake `AGENT`, transporte serial e IPC local mínimo — sem substituir o Python em produção.

**Architecture:** Cargo workspace: `protocol` (golden lines), `usb` (CDC + DTR), `profile` (machine_id + config), `agentd` bin (loop + HTTP/unix IPC). Capabilities completas ficam no plano de parity; aqui só stub `AGENT` + eco/`AITOKENS?` no-op documentado.

**Tech Stack:** Rust 1.83 (mise), `serialport`, `tokio`, `serde`/`toml`, `axum` ou `hyper` em localhost, `clap`.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-10-25-tessera-agent-v2-design.md`
- Wire format idêntico ao Python (`\t`, `\n`, prefixos)
- Paths: `~/.tessera/agentd/` e LaunchAgent `com.tessera.agentd` (não `com.tessera.agent`)
- Pré-req: plano mise aplicado (`mise.toml` existe)
- Idioma de logs user-facing: EN técnico no log; Doctor UI depois

---

### Task 1: Scaffold do workspace Cargo

**Files:**
- Create: `agentd/Cargo.toml`
- Create: `agentd/crates/protocol/Cargo.toml`
- Create: `agentd/crates/protocol/src/lib.rs`
- Create: `agentd/crates/usb/Cargo.toml`
- Create: `agentd/crates/usb/src/lib.rs`
- Create: `agentd/crates/profile/Cargo.toml`
- Create: `agentd/crates/profile/src/lib.rs`
- Create: `agentd/crates/agentd/Cargo.toml`
- Create: `agentd/crates/agentd/src/main.rs`
- Modify: `scripts/mise/check-agent-trees.sh` (já passa se `agentd/` existe)

**Interfaces:**
- Produz: `cargo test` no workspace; bin `agentd --help`

- [ ] **Step 1: Workspace root `agentd/Cargo.toml`**

```toml
[workspace]
resolver = "2"
members = [
  "crates/protocol",
  "crates/usb",
  "crates/profile",
  "crates/agentd",
]

[workspace.package]
edition = "2021"
license = "MIT"
version = "0.1.0"

[workspace.dependencies]
serde = { version = "1", features = ["derive"] }
serde_json = "1"
toml = "0.8"
thiserror = "2"
tokio = { version = "1", features = ["rt-multi-thread", "macros", "net", "io-util", "sync", "time"] }
```

- [ ] **Step 2: Crate `protocol` mínimo**

`crates/protocol/src/lib.rs`:

```rust
//! USB line codec — Tessera agent ↔ panel.

pub fn agent_ok_line(version: &str) -> String {
    // Match legacy spirit: AGENT\tv1\t1 (ok) — keep fields documented in tests
    format!("AGENT\tv1\t1\t{version}")
}

pub fn parse_prefix(line: &str) -> &str {
    line.split('\t').next().unwrap_or("")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn agent_line_has_tabs() {
        let s = agent_ok_line("0.1.0");
        assert!(s.starts_with("AGENT\tv1\t1\t"));
        assert_eq!(parse_prefix(&s), "AGENT");
    }
}
```

`crates/protocol/Cargo.toml`:

```toml
[package]
name = "tessera-protocol"
version.workspace = true
edition.workspace = true

[dependencies]
```

- [ ] **Step 3: Stubs `usb` + `profile`**

`usb/src/lib.rs`:

```rust
//! Serial CDC helpers (DTR on open — required by ESP32 CDC).

pub struct PortName(pub String);

pub fn list_candidate_ports() -> Vec<PortName> {
    // Implement with serialport::available_ports in Task 3
    Vec::new()
}
```

`profile/src/lib.rs`:

```rust
use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Debug, Serialize, Deserialize)]
pub struct MachineProfile {
    pub machine_id: String,
    pub display_name: String,
}

pub fn default_agentd_dir() -> PathBuf {
    dirs::home_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join(".tessera/agentd")
}
```

Add `dirs = "5"` to profile Cargo.toml dependencies.

- [ ] **Step 4: Bin `agentd`**

`crates/agentd/src/main.rs`:

```rust
use clap::Parser;

#[derive(Parser, Debug)]
#[command(name = "agentd", version, about = "Tessera Agent v2 daemon")]
struct Args {
    /// Print AGENT handshake line and exit (for golden/CI)
    #[arg(long)]
    print_agent_line: bool,
}

fn main() {
    let args = Args::parse();
    if args.print_agent_line {
        print!("{}", tessera_protocol::agent_ok_line(env!("CARGO_PKG_VERSION")));
        return;
    }
    eprintln!("agentd: USB loop not wired yet — see plan parity");
    std::process::exit(0);
}
```

Dependências do bin: `clap` (derive), `tessera-protocol`, path deps.

- [ ] **Step 5: Build + test**

```bash
cd agentd && mise exec -- cargo test && mise exec -- cargo run -p agentd -- --print-agent-line
```

Expected: tests pass; stdout começa com `AGENT\tv1\t1\t`.

- [ ] **Step 6: Commit**

```bash
git add agentd/
git commit -m "$(cat <<'EOF'
feat(agentd): scaffold Rust workspace with AGENT line codec

EOF
)"
```

---

### Task 2: Golden fixtures a partir do Python

**Files:**
- Create: `agentd/crates/protocol/tests/golden/`
- Create: `agentd/crates/protocol/tests/golden_media.rs` (ou um `golden.rs`)
- Create: `tools/agent_capture_golden.py` (script one-shot opcional)
- Create: `agentd/crates/protocol/tests/fixtures/agent_ok.txt`

**Interfaces:**
- Consome: formato documentado no README do agent legacy
- Produz: testes que falham se o encoder Rust divergir dos fixtures

- [ ] **Step 1: Fixture handshake**

`tests/fixtures/lines/agent_ok.txt` (uma linha, sem newline final opcional — documentar):

```
AGENT	v1	1
```

Nota: alinhar com o que `agent/src/agent/runtime.py` **realmente** emite hoje. Antes de fixar o fixture, rodar:

```bash
rg -n "AGENT\\\\t|AGENT\\\\tv" agent/src/agent -g'*.py'
```

Ajustar `agent_ok_line` e o fixture ao wire real (campos exatos).

- [ ] **Step 2: Teste golden**

```rust
#[test]
fn golden_agent_prefix() {
    let raw = include_str!("../fixtures/lines/agent_ok.txt").trim_end();
    assert_eq!(tessera_protocol::parse_prefix(raw), "AGENT");
}
```

Para cada capability crítica (`MEDIA`, `FOCUS`, `AITOKENS`), adicionar **pelo menos** um arquivo de linha capturada do Python (copiar de log real ou gerar com unit do Python). No mínimo neste task: `AGENT` + um `MEDIA` de exemplo.

- [ ] **Step 3: Documentar no `agentd/README.md`**

```markdown
# agentd

Daemon Rust (Agent v2). Coexiste com `agent/` Python até o cutover.

```bash
mise run agentd-test
mise run agentd-run
```

Golden lines: `crates/protocol/tests/fixtures/`.
```

- [ ] **Step 4: Commit**

```bash
git add agentd/ tools/agent_capture_golden.py
git commit -m "$(cat <<'EOF'
test(agentd): golden USB line fixtures for protocol crate

EOF
)"
```

---

### Task 3: USB open + DTR + read loop (smoke)

**Files:**
- Modify: `agentd/crates/usb/src/lib.rs`
- Modify: `agentd/crates/agentd/src/main.rs`
- Create: `agentd/crates/agentd/src/usb_loop.rs`

**Interfaces:**
- Consome: `serialport` crate
- Produz: `agentd --once` abre porta, seta DTR, escreve `AGENT` line, lê até 2s, sai

- [ ] **Step 1: Dependência `serialport = "4"` no crate usb**

- [ ] **Step 2: Implementar open com DTR**

```rust
pub fn open_tessera_port(path: &str) -> Result<Box<dyn serialport::SerialPort>, UsbError> {
    let mut port = serialport::new(path, 115_200)
        .timeout(std::time::Duration::from_millis(200))
        .open()
        .map_err(UsbError::from)?;
    // Critical: ESP32 CDC needs DTR (no reset pulse policy — match Python)
    let _ = port.write_data_terminal_ready(true);
    Ok(port)
}
```

Espelhar comentários de `agent/src/agent/runtime.py` sobre DTR.

- [ ] **Step 3: CLI flags**

```text
agentd --port /dev/cu.usbmodem* --once
```

Sem porta: lista candidatos cujo nome contém `usbmodem` / `Tessera` e imprime; exit 1 se `--once` sem match (dev-friendly).

- [ ] **Step 4: Teste unitário sem hardware**

Mock: função `write_handshake(w: &mut impl Write)` testada com `Vec<u8>`.

- [ ] **Step 5: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(agentd): USB open with DTR and once-shot handshake

EOF
)"
```

---

### Task 4: IPC local mínimo (status JSON)

**Files:**
- Create: `agentd/crates/agentd/src/ipc.rs`
- Modify: `main.rs`

**Interfaces:**
- Produz: `GET http://127.0.0.1:19792/v1/status` → `{ "ok": true, "version": "...", "usb": "disconnected"|"up" }`
- Porta configurável via `TESSERA_AGENTD_IPC_PORT` (default 19792)
- Bind **só** localhost

- [ ] **Step 1: axum router com `/v1/status` e `/v1/health`**

- [ ] **Step 2: Teste com `reqwest` ou `axum::test` (tokio)**

Expected: 200 + JSON com `version`.

- [ ] **Step 3: README: IPC contract curto**

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(agentd): localhost status IPC for companion UI

EOF
)"
```

---

### Task 5: Profile load/save + machine_id

**Files:**
- Modify: `agentd/crates/profile/src/lib.rs`
- Create: `agentd/crates/profile/src/config.rs`
- Test: `agentd/crates/profile/tests/config_roundtrip.rs`

**Interfaces:**
- Produz: `load_or_init() -> MachineProfile` em `~/.tessera/machine.json` + `~/.tessera/agentd/config.toml`
- `config.toml` keys: `media = true`, `focus = true`, `aitokens = true`, `calendar = true`

- [ ] **Step 1: Roundtrip test em tempdir**

```rust
#[test]
fn config_roundtrip() {
    let dir = tempfile::tempdir().unwrap();
    // write config, read back, assert media == true
}
```

- [ ] **Step 2: Implementação mínima**

- [ ] **Step 3: Na 1ª execução, gerar `machine_id` (UUID) e `display_name` default = hostname

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(agentd): machine profile and local config.toml

EOF
)"
```

---

## Spec coverage

| Spec | Task |
|---|---|
| Workspace agentd | 1 |
| Golden protocol | 2 |
| USB + DTR | 3 |
| IPC para UI | 4 |
| Perfil config | 5 |
| Capabilities full | → parity plan |

## Handoff

Seguir `2026-10-25-agent-v2-ui.md` (UI consome `/v1/status`) em paralelo possível com stubs de caps; parity depois.
