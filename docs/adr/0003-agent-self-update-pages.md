# Agent self-update via GitHub Pages + Releases

**Status:** accepted  
**Data:** 2026-10-26  
**Relacionado:** `docs/superpowers/specs/2026-10-25-tessera-site-branding-design.md`, `docs/superpowers/specs/2026-10-25-tessera-agent-v2-design.md`

## Contexto

O companion (agentd + Tessera.app) precisa atualizar no Mac do usuário sem depender de um agente de IA nem de reinstall manual por SD a cada mudança.

## Decisão

1. **Artefatos** ficam em **GitHub Releases** (tag `agent-vX.Y.Z`): `agentd.tgz`, `Tessera.app.zip` (ou DMG), checksums SHA-256.
2. **Manifest** público em GitHub Pages: `https://gilsondev.github.io/Tessara/latest.json` (`version`, URLs, `sha256`, `released_at`).
3. **agentd** faz poll no start e a cada ~6–12 h. Se a versão remota for maior **e** `auto_update = true` em `~/.tessera/agentd/config.toml`, baixa, verifica SHA-256, instala daemon + app e `launchctl kickstart -k com.tessera.agentd` — **sem prompt**.
4. Toggle **Atualizar sozinho** na aba Mais do agent-ui grava `auto_update`. Desligado: só reporta “nova disponível” via IPC.
5. Binários grandes **não** vão no Pages — só o JSON (+ site estático).

## Consequências

- **1ª instalação** (Mac novo): site Pages — botão ou
  `curl -fsSL https://gilsondev.github.io/Tessera/install.command | bash`.
  O painel e o portal apontam para esse URL; cartão SD é só fallback offline.
- CI `agent-build.yml` compila em todo push/PR relevante; `agent-release.yml` (tag `agent-v*`)
  publica Release **e** atualiza `site/public/latest.json` no deploy Pages.
- Notarização Apple fica fase 2; site avisa build não notarizado.
- Dev local: `mise run agent-install` (build + cutover), paralelo ao canal de release.
