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
