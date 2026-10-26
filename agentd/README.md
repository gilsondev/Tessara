# agentd

Daemon Rust (Agent v2). Coexiste com `agent/` Python até o cutover.

```bash
mise run agentd-test
mise run agentd-run
```

## Protocol parity

USB lines are tab-separated. Golden fixtures under `crates/protocol/tests/fixtures/lines/` were taken from the legacy Python encoders:

- `agent/src/agent/protocol.py` — `AGENT\tv1\t1` (OK handshake; **no** extra semver field; Python adds `\n` only when writing the serial port)
- `agent/src/agent/capabilities/media/__init__.py` — `MEDIA\tv1\t…` (see `crates/caps-media/README.md` for field order)

With `config.media = true`, the daemon USB loop answers `MEDIA?`, pushes on encoded line change (~2s poll), and sends an 8s heartbeat fallback (`MEDIA_PUSH_INTERVAL_S`).

Rust `tessara_protocol::agent_ok_line()` returns the same three fields as `agent_hello(ok=True)` before transport newline. Task 1 briefly added a fourth version field; that was removed here to match Python for cutover.

### CALENDAR wire

Calendar tokens remain only on the Mac under `~/.tessera/calendar/`; none are
sent over USB. With `config.calendar = true`, agentd answers:

- `CALENDAR?` → `CALENDAR\tv1\t1\tlinked` followed by
  `CALENDAR_CATALOG\tv1\t…`
- `CALENDAR_CATALOG?` → catalog/status line
- `CALENDAR_SEL\tid…` → persists the selected IDs locally, then returns a
  snapshot and catalog status
- `CALENDAR_AUTH?` → `CALENDAR_AUTH\tv1\t0\t…`

The initial Rust port encodes full event snapshots and safely reports missing
`client.json`, but does not yet open the OAuth browser or fetch Google events.
Without `~/.tessera/calendar/client.json`, it returns `falta client.json` /
`configure client.json`; USB requests never fail the session.

### AITOKENS caveats

`CursorResolver` contributes an explicit unavailable source when it finds local authentication but the Rust daemon has no live quota client; it never emits a fabricated percentage. Claude currently reads one fresh status/cache value, so Python's future multi-surface display format (`a · b`) is not yet reproduced.

Refresh fixtures from Python (optional):

```bash
python3 tools/agent_capture_golden.py
```

Golden lines: `crates/protocol/tests/fixtures/`.

## Localhost IPC

Companion UI reads daemon state over HTTP on **127.0.0.1 only** (not exposed on LAN).

| Env | Default |
|-----|---------|
| `TESSARA_AGENTD_IPC_PORT` | `19792` |

| Method | Path | Response |
|--------|------|----------|
| GET | `/v1/status` | `{ "ok", "version", "usb", "setup_completed", "machine": { "id", "display_name" } }` |
| GET | `/v1/health` | `{ "ok": true }` |
| POST | `/v1/scan` | Varredura local; persiste `scan.json` e atualiza `config.toml` (body opcional: `setup_completed`, `display_name`, `found[]`) |
| GET | `/v1/scan/last` | Último `scan.json` |
| GET | `/v1/machines` | `{ "machines": [{ "machine_id", "display_name", "this_machine" }] }` |

Example:

```bash
curl -s http://127.0.0.1:19792/v1/status
```

Without `--once` / `--print-agent-line`, `agentd` starts this server and keeps running until interrupted.
