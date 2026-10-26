# Todoist via Agent

HTTPS and token live on the Mac (`agentd` / `caps-todoist`). The panel only paints `task_store` and sends closes over USB.

## Wire (stable contract)

```
TODOIST\tv1\tok\tlinked\t<n>\t<name>\t<query>…     # catalog
TODOIST_TASKS\tv1\t<index>\t<count>\t<id>\t<prio>\t<title>\t<meta>…  # one or more lines per list
TODOIST_END\tv1\tok\tlinked\t<n>                   # commit
```

- Cap: **8 lists × 60 tasks** (same as the old Wi‑Fi path).
- Long lists are packed under ~3.5KB/line; the panel **appends** chunks.
- Panel asks: `TODOIST?` (cache) or `TODOIST?\tforce` (pull-to-refresh).
- Close: `TODOIST_CLOSE\t<id>` → `TODOIST_CLOSE\tv1\tok\tid`.

## Regression tests (must stay green)

| Layer | Command / case |
|---|---|
| Native | `make test` — `test_todoist_bridge_*` (overlap, append, 16-on-one-line, empty list) |
| Native | `test_home_count_shows_partial_while_lists_warm` — tile never stuck blank when some lists Ready |
| agentd | `cargo test -p tessera-caps-todoist` — encode all tasks, budget split, empty view |
| agentd | `cargo test -p agentd describe` / logutil — **token never echoed** |
| Host E2E | `make e2e` / `make finish-host` — simulator Todoist screen still mounts |

## Manual device check (`make device`)

1. Cable + agentd running → Activity shows `todoist · … lista(s)`, **never** a raw token.
2. Home tile shows a number (not empty / not stuck on `…` with data on Mac).
3. Open Todoist → every active list has the same task count as Todoist (up to 60).
4. Horizontal swipe between lists stays smooth after ~0.5s idle — frames are composed straight into
   the framebuffer (`PERF` shows `slides=…`); see [slide compositor](../superpowers/specs/2026-10-26-todoist-slide-compositor-design.md)
   for the measurement harness (`tools/probe_slide.py`) and its pitfalls.
5. Complete a task → row animates; Mac log `todoist · close-ack`.
6. Unplug USB → Wi‑Fi fallback only if token still in NVS (optional); with agent linked there is **no** `todoist TLS` / `sem DRAM` on the panel.

## Offline / stale

- With agent linked: panel keeps last `task_store` until the next USB bundle.
- Agent silent for 12s after a request: lists with cache keep it, empty lists say "Sem resposta do
  Mac." and the full-screen loader stops claiming data is on its way. Requests keep going out every
  5s (cold) / 45s (warm), so the screen recovers on its own.
- Without cable and without NVS token: show token needed / empty — do not invent tasks.
- SD cold cache is **fase 2** (not required for acceptance).

## Finish gate

After Todoist/agent USB changes: `make finish-host` before `make flash`. See [`finish-gate.md`](finish-gate.md).
