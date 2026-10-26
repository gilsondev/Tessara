# Agent v2 — gate manual (cutover)

Obrigatório **antes** de remover `agent/` Python do path de produto. Documente resultados no PR (data, painel, macOS).

Pré-req: `mise run agentd-test` verde; cutover com legacy desligado:

```bash
mise run agentd-build
mise run agent-ui-build
BOOTOUT_LEGACY=1 tools/cutover_agent_v2.sh --bootout-legacy
```

| # | Check | Como validar | OK | Notas |
|---|--------|--------------|----|-------|
| 1 | MEDIA dirty-push | Troque faixa no Mac com cabo USB; tile Música atualiza em **< 1 s** | [ ] | |
| 2 | FOCUS SET | No painel, SET de um modo; Focus muda no macOS | [ ] | |
| 3 | AITOKENS | Três fontes (Codex / Claude / Cursor) ou N/A coerente com o Mac | [ ] | |
| 4 | CALENDAR | Eventos no painel com conta linkada | [ ] | |
| 5 | Wizard | 2ª abertura do Tessera.app **sem** wizard | [ ] | |
| 6 | Doctor FDA | Revogue Full Disk Access; Doctor pede correção | [ ] | |
| 7 | Legacy off | `launchctl print gui/$(id -u)/com.tessera.agent` falha; só **Tessera** (`~/.tessera/agentd/bin/Tessera`) na USB | [ ] | |

**Assinatura gate:** _______________ **Data:** ___________

Até assinar: manter `agent/` no repo; `make push-agent` (Python) e `make push-agentd` (v2) em paralelo.

Referência: `docs/superpowers/specs/2026-10-25-tessera-agent-v2-design.md` (Critérios de cutover).
