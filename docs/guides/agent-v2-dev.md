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

### Instalar neste Mac (após mudanças no companion)

```bash
mise run agent-install
# ou: make agent-install
```

Rebuilda agentd + Tessera.app e roda `tools/cutover_agent_v2.sh` (LaunchAgent + `/Applications/Tessera.app`). Self-update em produção: Pages `latest.json` + toggle **Atualizar sozinho** (ADR `0003`).

Finish-gate do **firmware** continua em `make test` / `make e2e` (PlatformIO).
