# Authoring a widget

How to add a new Tessara widget package with the same rules as **clock** and **weather**.

Prerequisites: [`getting-started.md`](../getting-started.md), [`architecture.md`](architecture.md).

## Non-negotiables

| Rule | Why |
|---|---|
| One package under `firmware/widgets/<id>/` | Isolation; single registration point |
| Talk to the world only via `WidgetContext` | Testable; no hidden Wi‑Fi/board coupling |
| No Arduino / board / Wi‑Fi headers in the widget | Enforced by `test_widget_allowlist` |
| No product logic in `dashboard_shell` / controller | Shell stays thin |
| Summary + optional detail in the **same** package | Home stays compact |
| Namespaced settings | Portal + NVS `w_*` without collisions |
| Host + simulator must build the package | Same sources as device |

Todoist is a good **mutable** example; it is **not** MVP acceptance.

## Suggested layout

```text
firmware/widgets/mywidget/
  README.md                 # English: id, behaviour, settings keys
  mywidget_widget.h|.cpp    # TsWidget impl + manifest
  ui/                       # LVGL / Ts* views (summary + detail)
  mywidget/                 # optional: parse, cache, domain (host-testable)
```

Use `firmware/widgets/clock/` or `firmware/widgets/weather/` as the template. Copy structure, then rename — do not fork board code into the package.

## Checklist

### 1. Manifest

- Stable id, e.g. `org.tessara.mywidget`
- Title, size hints, `accent_rgb` if needed
- `home_tile` vs `hero_only` (heroes like greeting/date do not open detail)
- Settings schema for instance keys only (device-global keys stay in device settings)

### 2. Implement lifecycle

```text
mount(ctx)      → create UI under ctx parent; start subscriptions / jobs
on_resize(w,h)  → relayout
unmount()       → cancel jobs; drop UI; invalidate generation
```

Async HTTP/results must carry a **generation** (or equivalent). Discard stale callbacks after unmount / remount. Prefer `ctx.scheduler()` / `ctx.http()` over raw tasks.

### 3. Use only context services

- `ctx.time()` — clock sync state
- `ctx.http()` — HTTPS GET with bounds
- `ctx.settings()` — namespaced get/observe/save
- `ctx.scheduler()` — periodic refresh + cancel
- `ctx.logger()` — namespaced logs (no secrets)

### 4. UI

- Prefer `tessara::ui::Ts*` (UI Kit)
- Home shows a **summary**; detail is optional and opened by the shell
- Do not own a global `lv_screen`; mount into the parent the shell provides

### 5. Register once

- Add the package to the registry / build includes (`platformio.ini` `-I` and any central register list used by the project)
- Do **not** edit unrelated widgets
- Extend native tests if you add parse/cache logic (`test/test_native/`)

### 6. Portal settings (if any)

- Expose fields under the per-widget settings API
- Keep keys namespaced; document them in the widget `README.md`

### 7. Verify

```bash
make test
make finish-host
# inspect $SIM_OUT home + detail screenshots
make sim   # optional gesture check
```

PR expectation: finish-host green + note on screenshots / memory if UI-heavy.

## Allowlist

`test_widget_allowlist` fails if a widget includes forbidden headers. If the test fails, move I/O behind `WidgetContext` / host services instead of relaxing the allowlist.

## MVP reminder

Shipping acceptance for the product MVP remains **clock + weather + shell/SDK/sim/portal**. Extra widgets are welcome as optional packages.
