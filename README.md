# Tessara Labs POC (`todoist-panel`)

Spike on JC3248W535 used to **validate** Tessara ideas. Product specs and trunk live in **Terrara** (`~/Developer/Projects/gilsondev/Terrara`).

Canonical product name: **Tessara**.

## Layout (Tessara-shaped)

See [`docs/STRUCTURE.md`](docs/STRUCTURE.md). Summary:

| Path | Role |
|---|---|
| `firmware/` | Device app (`components/`, `widgets/`, `boards/`) |
| `agent/` | Mac Tessara Agent (USB; AI Tokens = 1st capability) |
| `simulator/` | Stub — implement next (Terrara SIMULATOR.md) |
| `web/` | Stub — portal HTML still in `web_configurator` |
| `boards/*.json` | PlatformIO board profile |
| `hardware/` | CAD cradle — not firmware |
| `docs/` | POC specs/plans + STRUCTURE |

## Build / flash

```bash
.venv/bin/pio run -e jc3248w535
./tools/flash.sh
```

## Agent (Mac)

```bash
cd agent && PYTHONPATH=src python3 -m agent
./pack_sd.sh /Volumes/YOUR_SD
```

## Do not

- Treat this tree as Tessara trunk (Terrara ADR-009).
- Copy `.pio/`, `.venv/`, or secrets into the product repo.
- Expand Todoist into Tessara MVP acceptance.
