# tessara-caps-media

USB `MEDIA` capability — encoder + watcher (parity with `agent/src/agent/capabilities/media/`).

## Wire format (tab-separated)

| Field | Index | Values |
|-------|-------|--------|
| Prefix | 0 | `MEDIA` |
| Version | 1 | `v1` |
| OK | 2 | `0` = failure / unavailable, `1` = success |
| State | 3 | `playing`, `paused`, or `stopped` (only when OK=1) |
| Title | 4 | sanitized, max 40 chars |
| Artist | 5 | sanitized, max 40 chars |
| App | 6 | sanitized, max 24 chars |

**OK=0:** `MEDIA\tv1\t0\t\t\t\t` (four empty trailing fields).

**OK=1 example:** `MEDIA\tv1\t1\tplaying\tTest Song\tTest Artist\tMusic`

Sanitization: tabs/newlines → space, trim, truncate with `…` at limit−1.

## Panel → agent

- `MEDIA?` — refresh metadata line
- `MEDIA\tPLAYPAUSE`, `MEDIA\tNEXT`, `MEDIA\tPREV` / `PREVIOUS` — transport (stub in v1)
- `MEDIA_COVER` — ignored (no blocking cover fetch in first ship)

## Push

When `config.media` is enabled: poll the watcher every 2s and push immediately if the encoded line changed; 8s heartbeat still pushes as fallback (matches legacy push-on-dirty + `MEDIA_PUSH_S`).
