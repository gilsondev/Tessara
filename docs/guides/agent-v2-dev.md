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

Não crie a tag `agent-v*` na mão e não edite `version` nos manifests.

1. Commits em `main` usam `feat:` ou `fix:`.
2. O workflow `release-please.yml` abre um pull request `chore: release X.Y.Z` que atualiza `VERSION` e `CHANGELOG.md`.
3. O merge cria a tag `agent-vX.Y.Z`.
4. `agent-release.yml` carimba os manifests no runner, publica o GitHub Release e grava `site/public/latest.json` em `main`.
5. `pages.yml` publica o site.

Build contínuo, sem publicar: `agent-build.yml`.

## Offline / SD (opcional)

```bash
make pack-agentd
# → tessera/agentd.tgz flat para PUT no portal (fallback sem internet)
```

Finish-gate do **firmware** continua em `make test` / `make e2e` (PlatformIO).
