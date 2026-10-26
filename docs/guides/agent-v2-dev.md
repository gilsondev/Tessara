# Agent — desenvolvimento (agentd)

Spec: `docs/superpowers/specs/2026-10-25-tessera-agent-v2-design.md`

O companion Mac é **só** `agentd/` (Rust) + `apps/agent-ui/` (Tessera.app). O runtime Python em `agent/` foi removido.

## Toolchain

```bash
mise trust
mise install
mise run agent-check-trees
```

## Build / teste / run

```bash
mise run agentd-test
mise run agentd-run
mise run agent-ui-dev
```

## Instalar neste Mac

```bash
mise run agent-install
# ou: make agent-install
```

Rebuilda agentd + Tessera.app e roda `tools/cutover_agent_v2.sh` (LaunchAgent `com.tessera.agentd` + `/Applications/Tessera.app`). Self-update: Pages `latest.json` + toggle **Atualizar sozinho** (ADR `0003`).

## Pacote SD / portal

```bash
make pack-agentd
# → agentd/dist/sd/tessera/agentd/{agentd.tgz,install.command,…}
# → agentd/dist/sd/tessera/agentd.tgz  (flat — o que o portal PUT/GET usa)
```

No painel: página **Agente** → upload `agentd.tgz`, depois `curl -fsSL http://painel/agentd/install.command | bash`.

Finish-gate do **firmware** continua em `make test` / `make e2e` (PlatformIO).
