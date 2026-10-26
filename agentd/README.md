# agentd

Daemon Rust (Agent v2). Coexiste com `agent/` Python até o cutover.

```bash
mise run agentd-test
mise run agentd-run
```

## Protocol parity

USB lines are tab-separated. Golden fixtures under `crates/protocol/tests/fixtures/lines/` were taken from the legacy Python encoders:

- `agent/src/agent/protocol.py` — `AGENT\tv1\t1` (OK handshake; **no** extra semver field; Python adds `\n` only when writing the serial port)
- `agent/src/agent/capabilities/media/__init__.py` — `MEDIA\tv1\t…`

Rust `tessara_protocol::agent_ok_line()` returns the same three fields as `agent_hello(ok=True)` before transport newline. Task 1 briefly added a fourth version field; that was removed here to match Python for cutover.

Refresh fixtures from Python (optional):

```bash
python3 tools/agent_capture_golden.py
```

Golden lines: `crates/protocol/tests/fixtures/`.
