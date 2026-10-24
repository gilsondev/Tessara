# Plan: architecture isolation lift

> **For agentic workers:** implement in order; keep features; verify host then device.

**Spec:** `docs/superpowers/specs/2026-10-24-architecture-isolation-lift-design.md`

## Task 1: Spec + STRUCTURE

- [x] Spec acima + atualizar `docs/guides/architecture-lift.md` (L6/ports status)

## Task 2: WidgetManifest flags + runtime

- [x] `hero_only` no manifest; registrar no `RegistryEntry`
- [x] `Runtime::type_hero_only` / `type_registered`
- [x] greeting/date = `hero_only: true`; demais false
- [x] Native: test hero_only + open ignore

## Task 3: Nav genérico

- [x] `Screen::{Home, Detail, Settings}` + `widget_id[48]`
- [x] `open_detail(state, id)`; wrappers `open_todoist` etc. → detail
- [x] Atualizar `test_nav` + controller rest restore via `widget_id`

## Task 4: Controller fino

- [x] `on_open_widget` só registry + hero_only + open_detail + show_widget
- [x] Sem bridge de paint no open — weather/aitokens pintam no `mount`

## Task 4b: Fechar lacunas do diagrama (pós-checklist)

- [x] Manifest `home_tile` + `accent_rgb`; home escolhe tile pelo registry
- [x] `settings` via `StoragePort` (NVS atrás do port)
- [x] `list_prefs` / `pending_closes` via `StoragePort`
- [x] Portal settings namespaced por widget
- [x] Todoist/AI Tokens rebuild no slot
- [x] Settings panel começa a usar `tessara::ui::Ts*` (+ home)

## Task 5: Ports HAL

- [x] `BacklightPort`, `PowerPort`, `StoragePort`, `Capabilities`
- [x] `using InputPort = TouchPort`
- [x] Display sem brightness; board JC implementa ports
- [x] `display_host` / controller / `power` usam ports

## Task 6: Ts UI Kit aliases

- [x] `ui/ts_kit.h` — `tessara::ui::TsCard` etc. wrapping `kit::`

## Task 7: Shell

- [x] `show_widget` → `mount_exclusive`

## Task 8: Verify

- [x] `make test` + `make e2e` + `make flash` smoke
