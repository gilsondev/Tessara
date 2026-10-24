# Finish gate

Fixed order after any feature, bugfix, or UI change. Source of truth: root **`Makefile`** (`make help`).

```bash
cd /path/to/Tessara
make help
```

## Steps

| Order | Make target | What it does |
|------:|---|---|
| 1 | `make test` | Automated suite (`.venv/bin/pio test -e native`) |
| 2 | `make e2e` | Simulator E2E (`simulator/scripts/verify_all.sh`). Inspect `$SIM_OUT` (default `/tmp/tessara-sim-out/`) |
| 3 | `make flash` | Program the JC3248 (`tools/flash.sh`). Only after 1 and 2 are green |
| 4 | `make device` | USB monitor for manual on-device checks. Ctrl+C exits |

## Shortcuts

```bash
make finish        # e2e → flash  (e2e already includes native tests)
make finish-host   # e2e without flash (CI / no board)
make device
```

Equivalent: `./tools/finish.sh` (`SKIP_FLASH=1` = finish-host).

## Extras

| Make | Use |
|---|---|
| `make sim-build` | Build the SDL binary |
| `make sim-run` | Run `./simulator/build/painel --scenario $SCENARIO …` |
| `make sim` | Build + run (default `SCENARIO=normal`) |
| `make verify` | Alias of `make e2e` |
| `make device-snap` | SNAP → `$SIM_OUT/device-snap.png` |
| `make monitor` | Alias of `make device` |

## Related

- Cursor rule: `.cursor/rules/finish-gate.mdc`
- Agents: `AGENTS.md`
- Simulator: [`simulator.md`](simulator.md)
- Architecture: [`architecture.md`](architecture.md)
