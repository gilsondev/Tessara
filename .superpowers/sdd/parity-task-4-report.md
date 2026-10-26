# Parity Task 4 — CALENDAR

- Added `tessara-caps-calendar` with `CALENDAR\tv1` event encoder and golden.
- USB loop now routes `CALENDAR?`, catalog, selection, and auth commands.
- Selection remains local; no OAuth tokens leave the Mac.
- Missing `~/.tessera/calendar/client.json` returns clear non-fatal status lines.
- OAuth browser flow and Google event fetching are deliberately stubbed.
- Verified: calendar crate tests and `cargo check -p agentd`.
- `make test`/`make e2e` are blocked: the worktree has no `.venv/bin/pio`.
