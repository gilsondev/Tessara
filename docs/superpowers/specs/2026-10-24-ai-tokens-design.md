# AI Tokens

**Depende de:** grilling fechado (Q1–Q24). Glossário: `GLOSSARY.md`. ADR: `docs/adr/0002-ai-tokens-usb-bridge.md`.

O subsistema mostra uso/cotas locais de agentes no Mac ligado ao Tessara. O painel **não** chama APIs das provedoras.

## O que a pessoa vê

- **Cartão de AI Tokens (home).** No fim da home, depois da linha Tarefas|Clima. Ícone no firmware + título "AI Tokens" + linhas só das Fontes coletadas. Codex: % restante 5 h + horário de reset. Cursor: soma dos % usados. Sem Bridge: “ligar Bridge”. **desatualizado** só após ~3 min sem push.
- **Tela de AI Tokens.** Topbar com voltar + refresh. Codex: barras 5 h e semana (% restante). Cursor: Modelos/Outros (% usados). Refresh manda `AITOKENS?`.
- **Configurações no aparelho:** entrada **AI Tokens** com checagem de status do Bridge (desligado / ok / sem cotas / desatualizado) e linhas das Fontes do último snapshot. Sem ligar/desligar Fonte no aparelho (Q16).
- **Página do celular:** sem toggles de Fonte neste MVP (Q16/Q22); limites manuais ficam para depois (Q4).

## Métricas

| Fonte | Origem no Mac | No cartão/tela |
|---|---|---|
| Codex | ChatGPT `wham/usage` (`primary` 5 h + `secondary` semana) | Home: **% restante 5 h** + **hora** do reset; detalhe: 5 h e semana |
| Claude | statusLine → JSON local (`percent` 5h preferido, senão 7d) | **% restante** + **hora** do reset |
| Cursor | `GetCurrentPeriodUsage` + `GetPlanInfo` (token do IDE) | Dois pools como no Settings: **% usados** Modelos (`autoPercentUsed`) · Outros (`apiPercentUsed`) + **data** do fim do ciclo. Alertas usam o pior restante (`100 − max(usados)`). Não usar `totalPercentUsed` nem `displayMessage`. |

Claude **indisponível** se o statusLine não estiver configurado. Codex/Cursor **indisponível** se o login local falhar.
## Bridge (Mac)

- Pasta: `tools/aitokens_bridge/` (Python).
- Sobe com **launchd** no login.
- Empurra snapshot a cada **~5 s** pela USB (115200).
- Responde a pedido do painel (`AITOKENS?`).
- **Solta a porta** se flash/probe/monitor a ocupar; retoma sozinho.
- Estrutura modular (potencial agente Tessara depois); MVP só cotas — sem LLM.
- Inclui script/doc de statusLine macOS para Claude.

## Protocolo USB

Linhas `\n`, tab-separadas. **Não** reusa o comando `TOKEN` (Todoist).

**Push / resposta:**

```
AITOKENS\tv1\t<bridge_ok>\t<id>\t<available>\t<kind>\t<value>\t<display>\t<reset>\t…
```

- `bridge_ok`: `0`|`1`
- Por Fonte (ordem livre, ids `codex`, `claude`, `cursor`):
  - `available`: `0`|`1`
  - `kind`: `tokens` | `pct` | `none`
  - `value`: inteiro (tokens ou percent ×1)
  - `display`: texto curto sem tab (o que pintar)
  - `reset`: rótulo curto ou vazio (ex. `06:10`)

**Pedido do painel:** `AITOKENS?`

## Frescor no painel

- Snapshot só em **RAM**. Reboot → indisponível até o 1º push.
- Sem linha nova por **~3 min** e já houve dado → **desatualizado**.
- Nunca houve dado → **indisponível**.

## Instalação no Mac

Cotas vêm pelo **Agente Tessara** (não mais pelo bridge legado). Ver `2026-10-24-tessera-agent-design.md` e `agent/README.md`.

```bash
# pack no SD do painel, depois baixar instalador na web do aparelho — ou, em dev:
bash agent/dist/sd/tessera/agent/install.command
```

Label launchd: `com.tessera.agent` (migra/remove `com.tessera.aitokens-bridge`).

## Fora do MVP

Mais Fontes além de Codex/Claude/Cursor, gráficos/histórico, toggles de Fonte na página do celular, persistir snapshot, agente LLM no Bridge.
