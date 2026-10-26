# Agent v2 — gate de cutover (fechado)

**Status:** cutover concluído — o companion de produto é `agentd` + Tessera.app. O runtime Python (`agent/`, `com.tessera.agent`) foi removido do tronco.

Checklist histórico (assinatura prática na branch `feat/agent-v2-impl`, 2026-10-26):

| # | Check | Resultado |
|---|--------|-----------|
| 1 | MEDIA dirty-push | OK no uso diário com agentd |
| 2 | FOCUS SET | OK via capability + Doctor |
| 3 | AITOKENS | OK (Codex / Claude / Cursor conforme o Mac) |
| 4 | CALENDAR | OK — janela hoje+próximos dias |
| 5 | Wizard | `setup_completed` + 2ª abertura sem wizard |
| 6 | Doctor FDA | OK (pede FDA no binário **Tessera**) |
| 7 | Legacy off | `com.tessera.agent` ausente; só `com.tessera.agentd` na USB |

Notas da sessão anterior: [`agent_v2_gate-results-2026-10-25.md`](agent_v2_gate-results-2026-10-25.md).

Instalar / revalidar neste Mac:

```bash
mise run agentd-test
make agent-install
```

Referência: `docs/superpowers/specs/2026-10-25-tessera-agent-v2-design.md`.
