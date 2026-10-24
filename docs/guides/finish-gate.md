# Finish gate — fechar implementação

Ordem fixa depois de qualquer feature/bugfix/UI. Fonte operacional: **`Makefile`** na raiz do `todoist-panel`.

```bash
cd todoist-panel
make help
```

## Tasks

| Ordem | Make | O que faz |
|------:|------|-----------|
| 1 | `make test` | Testes automatizados (`.venv/bin/pio test -e native`). Crie/atualize cobertura do comportamento novo antes de rodar. |
| 2 | `make e2e` | E2E no simulador: `simulator/scripts/verify_all.sh` (nativos + build `painel` + screenshots headless). **Inspecione** BMP/PNG em `$SIM_OUT` (default `/tmp/tessara-sim-out/`) contra o design pedido. Estenda fixtures/`--screen` se a mudança for tela ou fluxo novo. |
| 3 | `make flash` | Grava no JC3248 (`tools/flash.sh`). Só depois de 1 e 2 verdes. Sem porta → reportar bloqueio; não marcar como validado no aparelho. |
| 4 | `make device` | Abre o monitor USB do painel para **teste manual** (olhar as telas no JC3248). Ctrl+C sai. |

## Atalhos

```bash
make finish        # e2e (já inclui nativos) → flash
make finish-host   # só e2e (CI / sem placa)
make device        # depois do flash: serial + checklist de telas
```

`make e2e` / `verify_all.sh` já executam a suite native; use `make test` isolado enquanto itera nos testes. Equivalente shell: `./tools/finish.sh` (`SKIP_FLASH=1` = `finish-host`).

## Extras

| Make | Uso |
|------|-----|
| `make sim-build` | Só compila o SDL |
| `make sim-run` | `./simulator/build/painel --scenario $$SCENARIO --fixtures simulator/fixtures` |
| `make sim` | `sim-build` + `sim-run` (default `SCENARIO=normal`) |
| `make verify` | Alias de `make e2e` |
| `make device-snap` | Captura framebuffer do aparelho (`SNAP` → `$SIM_OUT/device-snap.png`) |
| `make monitor` | Alias de `make device` |

## Regras relacionadas

- Cursor alwaysApply: `.cursor/rules/finish-gate.mdc`, `.cursor/rules/promotion-path.mdc`
- Agent: `AGENTS.md`
- Simulador: `docs/guides/simulator.md`
- Promoção Labs → Tessara: `docs/guides/promotion-to-tessara.md`
