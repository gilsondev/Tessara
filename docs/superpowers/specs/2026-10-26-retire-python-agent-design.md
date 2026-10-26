# Retirada do agente Python (`agent/`)

**Data:** 2026-10-26  
**Status:** feito nesta branch  
**Fecha:** critério 6 do cutover em [`2026-10-25-tessera-agent-v2-design.md`](2026-10-25-tessera-agent-v2-design.md)

## O que saiu

- Árvore `agent/` (runtime Python, `pack_sd.sh`, requirements).
- Task mise `agent-legacy-run` + script.
- Pack / portal apontando para `agent.tgz` + LaunchAgent `com.tessera.agent`.

## O que ficou no lugar

| Antes | Depois |
|---|---|
| `agent/pack_sd.sh` → `agent.tgz` | `agentd/pack_sd.sh` → `agentd.tgz` (+ flat `tessera/agentd.tgz`) |
| `PUT/GET /agent/agent.tgz` | `PUT/GET /agentd/agentd.tgz` (aliases `/agent/…` ainda aceitos no GET/PUT) |
| `install.command` Python + venv | `install.command` agentd (binário Tessera + `com.tessera.agentd`) |
| SD `/tessera/agent.tgz` | SD `/tessera/agentd.tgz` + `agentd.version` |

`tools/cutover_agent_v2.sh` continua fazendo bootout de `com.tessera.agent` se ainda existir no Mac (migração de instalações antigas), sem depender do código Python no repo.

## Verificação

- `mise run agent-check-trees` — falha se `agent/` voltar
- `make test` · `make finish-host`
- Portal: página Agente fala em `agentd.tgz`
