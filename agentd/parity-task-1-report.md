# Parity MEDIA — Task 1 report

## push-on-dirty (gap fix)

- USB loop: poll watcher every 2s (`MEDIA_POLL_INTERVAL_S`); push when encoded line ≠ `last_pushed`; 8s heartbeat still forces push.
- `caps-media`: `media_line_changed`, `note_pushed`, unit test `media_line_changed_compares_full_wire`.
- **Device Step 5 (JC3248):** deferred — not exercised on hardware in this pass.
