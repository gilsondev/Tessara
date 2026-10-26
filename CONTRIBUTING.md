# Contributing to Tessera

Thank you for helping. English is the default for code, comments, commits, and docs. Optional Portuguese mirrors live under `docs/pt/`.

## Before you start

1. Read [`docs/getting-started.md`](docs/getting-started.md) and run `./tools/bootstrap-env.sh` (or the manual steps there).
2. Read [`docs/guides/architecture.md`](docs/guides/architecture.md) — widgets stay in their packages; the shell has no product logic.
3. Confirm `make finish-host` is green on `main` before branching.

## Development loop

```bash
# work…
make test           # fast feedback
make finish-host    # required before claiming done / opening a PR
make sim            # optional interactive check
```

With a board (after host gate is green): `make flash` then `make device`.

## Rules of the road

- **Finish gate:** `test` → `e2e` → `flash` → `device`. See [`docs/guides/finish-gate.md`](docs/guides/finish-gate.md).
- **Widgets:** new behaviour goes in `firmware/widgets/<name>/` via the Widget SDK only. Do not add product logic to `dashboard_shell` / controller. Guide: [`docs/guides/widget-authoring.md`](docs/guides/widget-authoring.md).
- **No secrets:** never commit `include/secrets.h`, `.pio/`, `.venv/`, credentials, or Wi‑Fi passwords.
- **Simulator is the host harness**, not a second UI. Prefer shared firmware sources; host adapters live under `simulator/host/`.
- **MVP scope:** clock + weather + shell/SDK/sim/portal. Extra widgets are welcome as optional packages; do not block MVP on them.

## Pull requests

1. One focused change per PR when possible.
2. Describe *why*; link issues if any.
3. Attach or point to simulator screenshots under `$SIM_OUT` when UI changes.
4. State that `make finish-host` passed (and `make flash` / device notes if hardware was used).
5. Keep comments and new docs in **English**.

## License

By contributing, you agree that your contributions are licensed under the
[Apache License, Version 2.0](LICENSE) (same as the project). Keep third-party
code’s original licenses intact and update [`NOTICE`](NOTICE) when you add
attribution-worthy dependencies.

## Questions

Open an issue (when the public tracker exists) or discuss with the maintainer. Agent-oriented notes: [`AGENTS.md`](AGENTS.md).
