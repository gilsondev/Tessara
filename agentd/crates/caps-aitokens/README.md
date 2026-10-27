# AITOKENS capability

The USB payload is a tab-separated snapshot with no newline in the encoder
(the USB transport appends it):

```text
AITOKENS\tv1\t<bridge_ok>\t<id>\t<available>\t<kind>\t<value>\t<display>\t<reset>...
```

`bridge_ok` is `1` when at least one source is available. Each source always
occupies six fields and legacy order is `codex`, `claude`, `cursor`.

| Field | Meaning |
| --- | --- |
| `id` | Provider identifier (`codex`, `claude`, or `cursor`) |
| `available` | `1` for a verified quota, otherwise `0` |
| `kind` | `pct`, `tokens`, or `none` |
| `value` | Primary remaining quota (integer; `0` if unavailable) |
| `display` | Panel label, e.g. `72% · 91%` |
| `reset` | Provider reset label, or empty |

Example:

```text
AITOKENS	v1	1	codex	1	pct	72	72% · 91%	14:30	claude	0	none	0	—		cursor	1	pct	87	10% · 13%	28 out
```

## Collection status

- **Cursor:** resolves credentials in legacy order: Cursor `state.vscdb`,
  macOS Keychain, Cursor `auth.json`, then `CURSOR_API_KEY` / `CURSOR_TOKEN`.
  Live refresh and Cursor usage API are not yet ported, so a discovered
  credential remains unavailable rather than reporting a made-up quota.
- **Claude:** reads Claude Desktop's local storage, a Claude Code statusLine
  file newer than two minutes (`~/.tessera/claude_status.json`), then the last
  known reading in `claude_usage_cache.json` for up to seven days. The panel
  shows percent remaining.
- **Codex:** detects `~/.codex/auth.json`; the ChatGPT WHAM usage client is
  pending, therefore it remains unavailable until a live response is parsed.

When `config.aitokens = true`, `agentd` answers `AITOKENS?` and pushes a
snapshot every 30 seconds.
