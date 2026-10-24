# Finish gate — fechar implementação

Ordem fixa depois de qualquer feature/bugfix/UI. Fonte: **`Makefile`** na raiz deste repo.

```bash
cd ~/Developer/Projects/gilsondev/Tessara
make help
```

## Tasks

| Ordem | Make | O que faz |
|------:|------|-----------|
| 1 | `make test` | Testes automatizados (`.venv/bin/pio test -e native`). |
| 2 | `make e2e` | E2E no simulador (`simulator/scripts/verify_all.sh`). Inspecione `$SIM_OUT` (default `/tmp/tessara-sim-out/`). |
| 3 | `make flash` | Grava no JC3248 (`tools/flash.sh`). Só depois de 1 e 2 verdes. |
| 4 | `make device` | Monitor USB para teste manual nas telas. Ctrl+C sai. |

## Atalhos

```bash
make finish        # e2e → flash
make finish-host   # só e2e (sem placa)
make device
```

`make e2e` já roda a suite native. Equivalente: `./tools/finish.sh` (`SKIP_FLASH=1` = finish-host).

## Extras

| Make | Uso |
|------|-----|
| `make sim-build` | Compila o SDL |
| `make sim-run` | `./simulator/build/painel --scenario $SCENARIO --fixtures simulator/fixtures` |
| `make sim` | build + run (default `SCENARIO=normal`) |
| `make verify` | Alias de `make e2e` |
| `make device-snap` | SNAP → `$SIM_OUT/device-snap.png` |
| `make monitor` | Alias de `make device` |

## Relacionados

- Cursor: `.cursor/rules/finish-gate.mdc`, `.cursor/rules/architecture.mdc`
- Agent: `AGENTS.md`
- Simulador: `docs/guides/simulator.md`
- Arquitetura: `docs/guides/architecture.md`
