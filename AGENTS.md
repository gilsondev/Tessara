# Tessara — agent harness

This repository is the product trunk for firmware / simulator / agent / portal.

Human onboarding: [`docs/getting-started.md`](docs/getting-started.md) · [`CONTRIBUTING.md`](CONTRIBUTING.md).  
Docs default to **English** (code and comments too). Optional PT: `docs/pt/`.

## Architecture

- Widgets live in `firmware/widgets/` — own package; Widget SDK + UI Kit only.
- Shell (`dashboard_shell`) — slots, nav, theme; **no** per-widget product logic.
- Contract: [`docs/guides/architecture.md`](docs/guides/architecture.md), [`docs/guides/widget-authoring.md`](docs/guides/widget-authoring.md).
- Glossary / ADRs: `GLOSSARY.md`, `docs/adr/`.
- Sibling Terrara specs are owner product docs; do not assume that absolute path exists for every contributor.

Todoist is not product MVP acceptance; it is a mutable widget pattern.

## Finish gate (always)

After any implementation (`make help`):

1. **`make test`** — PlatformIO `native`
2. **`make e2e`** — simulator; inspect `$SIM_OUT` (default `/tmp/tessara-sim-out/`)
3. **`make flash`** — only when 1 and 2 are green
4. **`make device`** — USB monitor / manual check on JC3248

```bash
./tools/bootstrap-env.sh   # once per machine (after OS packages)
make finish-host           # e2e without board
make finish                # e2e + flash
make device
make sim-run               # SDL window
```

Doc: [`docs/guides/finish-gate.md`](docs/guides/finish-gate.md). Cursor rule: `.cursor/rules/finish-gate.mdc`.

## Do not

- Commit `include/secrets.h`, `.pio/`, `.venv/`
- Treat Labs / POC as the trunk
- Add product logic to the controller / shell
