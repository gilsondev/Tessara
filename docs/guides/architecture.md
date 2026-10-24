# Arquitetura — contrato de widgets

Norte: `Terrara/docs/specs/` (ARCHITECTURE, WIDGET-SDK, SIMULATOR).

## Estado atual

- `widget_sdk` — `TsWidget` / `WidgetManifest` (`hero_only`, `home_tile`, `accent_rgb`) / `WidgetContext` (time, settings, http, scheduler, logger) + `MemoryHost`
- `widget_runtime` — registry + `mount_exclusive` + generation cancel
- Pacotes: `firmware/widgets/{weather,todoist,aitokens,clock,greeting,date}` (greeting/date = hero-only)
- Nav: `Screen::{Home,Detail,Settings}` + `widget_id`
- Controller abre detalhe só via registry (`type_registered` / `type_hero_only`)
- Ports HAL: Display, Input, Backlight, Power, Sound, Button, Storage, Capabilities — board JC
- Ts UI Kit: `tessara::ui::Ts*` sobre `kit::`
- Home: compose ∩ registry; tiles pelo `home_tile` do manifesto
- Settings de widget namespaced (`/api/widget_settings`, NVS `w_*`)
- `list_prefs` / `pending_closes` via `StoragePort`
- Sim faults + `make e2e` / `verify_all`

## Contrato de widget

Cada widget é um **pacote** que:

1. Declara manifest (`id`, título, tamanho, accent, schema de settings)
2. Implementa `mount` / `on_resize` / `unmount` no slot do shell
3. Pede dados só por **WidgetContext**
4. Tem **summary** (home) + **detail** opcional no mesmo pacote
5. Settings **namespaced** (portal + painel; device vs widget)
6. Compila no host e no sim **sem** headers de board/Wi‑Fi
7. Registra-se com **uma** entrada — sem editar outros widgets

O shell só: slots, navegação, tema, status (Wi‑Fi/hora).

Abertura:

```
on_open_widget(id):
  if !registered(id) → ignore
  if hero_only(id) → ignore
  nav.open_detail(id)
  ui.show_widget(id)   // mount_exclusive; widget pinta no mount
```

## Features novas

- Lógica nova de widget → só no pacote `widgets/X`, via ctx.
- Proibido acoplar produto novo no `controller`/shell.
- Novo widget: seguir clock/weather como template; Todoist = exemplo mutável, não aceite MVP.
