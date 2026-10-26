# Architecture — layers and widget contract

English is canonical. Product north-star specs may also live in a sibling Terrara docs tree (`ARCHITECTURE`, `WIDGET-SDK`, `SIMULATOR`); this guide describes **what is implemented in this repository**.

## Layers

```text
┌─────────────────────────────────────────────────────────┐
│  web/ portal  ·  agent/ (Mac USB)                        │
├─────────────────────────────────────────────────────────┤
│  dashboard_shell — slots, nav, theme, status (no product)│
├─────────────────────────────────────────────────────────┤
│  widgets/* — packages (clock, weather, …) via Widget SDK │
├─────────────────────────────────────────────────────────┤
│  widget_sdk + widget_runtime — manifest, mount, registry │
├─────────────────────────────────────────────────────────┤
│  ui_kit (Ts*) · core_services                            │
├─────────────────────────────────────────────────────────┤
│  HAL ports — Display, Input, Backlight, Power, Storage…  │
├─────────────────────────────────────────────────────────┤
│  boards/jc3248w535  ·  simulator/host adapters           │
└─────────────────────────────────────────────────────────┘
```

| Layer | Location | Responsibility |
|---|---|---|
| Widgets | `firmware/widgets/` | Product UI + data via `WidgetContext` only |
| Shell | `firmware/components/dashboard_shell/` | Slots, navigation, theme, Wi‑Fi/time chrome |
| Widget SDK | `firmware/components/widget_sdk/` | `TsWidget`, `WidgetManifest`, `WidgetContext`, `MemoryHost` |
| Runtime | `firmware/components/widget_runtime/` | Registry, `mount_exclusive`, generation cancel |
| UI Kit | `firmware/components/ui_kit/` | `tessera::ui::Ts*` over `kit::` |
| HAL | `firmware/components/hal/` | Board-agnostic ports |
| Board | `firmware/boards/jc3248w535/` | JC adapters |
| Simulator | `simulator/` | Same widget/shell sources + host ports + fixtures |

## Current building blocks

- Manifest fields include `hero_only`, `home_tile`, `accent_rgb`, `supports_home_tile_size`
- Shared home grid height: widget setting `home_tile_size` (`S`–`XL`) via `domain/home_tile_size.h`
- Context services: time, settings, http, scheduler, logger
- Nav: `Screen::{Home,Detail,Settings}` + `widget_id`
- Controller opens detail only through the registry (`type_registered` / `type_hero_only`)
- Home = compose ∩ registry; tiles follow `home_tile`
- Hidden (portal “Oculto”) = not in compose → no home LVGL tile; controller skips USB asks / Wi‑Fi fetches for that id until re-enabled
- Widget settings: namespaced (`/api/widget_settings`, NVS `w_*`)
- Host verification: `make e2e` / `simulator/scripts/verify_all.sh`

## Widget contract

Each widget is a **package** that:

1. Declares a manifest (`id`, title, size, accent, settings schema)
2. Implements `mount` / `on_resize` / `unmount` into a shell slot
3. Fetches data **only** through `WidgetContext`
4. Owns **summary** (home) and optional **detail** in the same package
5. Uses **namespaced** settings (portal + panel)
6. Builds on host and simulator **without** board / Wi‑Fi headers
7. Registers with **one** entry — do not edit other widgets’ packages

Shell responsibilities only: slots, navigation, theme, status.

```
on_open_widget(id):
  if !registered(id) → ignore
  if hero_only(id) → ignore
  nav.open_detail(id)
  ui.show_widget(id)   // mount_exclusive; widget paints in mount
```

## New features

- New product logic → `firmware/widgets/<x>/` via context only
- Do **not** couple new product behaviour into the controller / shell
- Templates: **clock** and **weather**. Todoist is a mutable reference widget, **not** MVP acceptance

Step-by-step: [`widget-authoring.md`](widget-authoring.md)
