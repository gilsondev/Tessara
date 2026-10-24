# Plan: portal UX + greeting/date widgets

> **For agentic workers:** implement tasks in order; run listed tests after each batch.

**Spec:** `docs/superpowers/specs/2026-10-24-portal-hero-widgets-design.md`

## Task 1: Dashboard ids + defaults

- Add `id_greeting()` / `id_date()`; drop hello from `known_type`.
- Defaults include greeting, date, clock + todoist, weather, aitokens.
- Native tests for parse/compose.

## Task 2: Hero widgets packages

- `firmware/widgets/greeting/`, `firmware/widgets/date/` — TsWidget, no detail UI.
- Register in `register_builtin`; home paints by `is_enabled`.
- Clock detail: keep time/date_long/sync; drop greeting label.
- Includes in platformio + sim CMake.

## Task 3: DeviceSettings AI provider flags + settings UI

- Persist three bools; filter home/aitokens_page/settings.
- Settings AI Tokens page: updated-at + toggles + Pedir cotas.
- Todoist page: primary green chips.

## Task 4: Portal `web/src/settings.html`

- Nav Início; ranges; sound toggle; AI Tokens section.
- `embed_web.py` via pio.

## Task 5: Verify

- `pio test -e native`
- Sim screenshots home/clock
- Flash + panel_probe smoke
