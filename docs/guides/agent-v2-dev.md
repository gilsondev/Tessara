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

## Release (CI → Pages)

1. `git tag agent-vX.Y.Z && git push origin agent-vX.Y.Z`
2. Workflow `agent-release.yml` sobe Release + grava `site/public/latest.json`
3. `pages.yml` publica o site; usuários instalam com o botão ou `install.command`

Build contínuo (sem publicar): `agent-build.yml` em push/PR.

## Offline / SD (opcional)

```bash
make pack-agentd
# → tessera/agentd.tgz flat para PUT no portal (fallback sem internet)
```

Finish-gate do **firmware** continua em `make test` / `make e2e` (PlatformIO).
