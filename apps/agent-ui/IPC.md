# agent-ui ↔ agentd IPC

HTTP on `127.0.0.1` only. Port: `TESSERA_AGENTD_IPC_PORT` (default **19792**), overridable in dev via `VITE_AGENTD_PORT`.

If agentd is offline or a route is missing, the UI falls back to embedded fixtures and shows a **demo** badge.

**Profile rule:** machine name, scan findings and capability toggles come from `~/.tessera/` via IPC. The UI must not invent roles (“Mac do trabalho”, etc.) — a clean install starts with `setup_completed=false` and fills the profile during welcome → scan → permissions.

| Method | Path | Shape |
|---|---|---|
| GET | /v1/status | `{ ok, version, usb, setup_completed, machine: { id, display_name } }` |
| POST | /v1/scan | `{ scanned_at, found: [{ id, state, enabled }] }` |
| GET | /v1/scan/last | último scan |
| GET | /v1/machines | `{ machines: [...] }` |
| GET | /v1/doctor | `{ summary, checks: [{ id, level, title, detail, action?, settings_pane?, settings_url? }] }` |
| POST | /v1/permissions/prepare | body `{ pane }` → `{ ok, pane, settings_url, binary_path, hint }` (registers Tessera in TCC, then UI opens Settings) |
| GET | /v1/panel/state | Portal snapshot (`settings`, `info`, `device_settings`, `widget_settings`, `widgets`, `weather`, `calendar`, `token_set`) via Wi‑Fi (`tessera.local`) |
| POST | /v1/panel/settings | Partial device settings → portal `/api/settings` (incl. `show_aitokens_*`) |
| POST | /v1/panel/widget_settings | `{ id, key, value }` → portal `/api/widget_settings` (ex.: `home_tile_size`) |
| POST | /v1/panel/dashboard | `{ widgets: [{ id, shown }] }` → portal `/api/dashboard` (ativar/ocultar na home) |
| POST | /v1/panel/refresh | Portal `/api/refresh` |
| GET | /v1/logs?tail=N | `{ lines: string[] }` |

## Notes

- **`/v1/status`**: `usb` is `"up"` or `"disconnected"`. Doctor and activity screens poll this every 2 s when visible.
- **`/v1/doctor`**: `level` drives row styling (`ok`, `warn`, `bad`, `skip`, …). Optional `action` is a screen id for `data-go`. When `settings_pane` is set (`full_disk_access`, `automation`, `login_items`, `calendar`), the UI shows **Abrir Ajustes** → prepare + deep link.
- **`/v1/logs`**: Plain log lines; activity **Avançado** renders them in the technical log panel. Default `tail=200`.

Client helpers live in `src/ipc.js`.
