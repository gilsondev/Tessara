# agent-ui ↔ agentd IPC

HTTP on `127.0.0.1` only. Port: `TESSARA_AGENTD_IPC_PORT` (default **19792**), overridable in dev via `VITE_AGENTD_PORT`.

Until agentd implements every route, the UI falls back to embedded fixtures and shows a **demo** badge.

| Method | Path | Shape |
|---|---|---|
| GET | /v1/status | `{ ok, version, usb, setup_completed, machine: { id, display_name } }` |
| POST | /v1/scan | `{ scanned_at, found: [{ id, state, enabled }] }` |
| GET | /v1/scan/last | último scan |
| GET | /v1/machines | `{ machines: [...] }` |
| GET | /v1/doctor | `{ summary, checks: [{ id, level, title, detail, action? }] }` |
| GET | /v1/logs?tail=N | `{ lines: string[] }` |

## Notes

- **`/v1/status`**: `usb` is `"up"` or `"disconnected"`. Doctor and activity screens poll this every 2 s when visible.
- **`/v1/doctor`**: `level` drives row styling (`ok`, `warn`, `bad`, `skip`, …). Optional `action` is a screen id for `data-go` (e.g. `setup`).
- **`/v1/logs`**: Plain log lines; activity **Avançado** renders them in the technical log panel. Default `tail=200`.

Client helpers live in `src/ipc.js`.
