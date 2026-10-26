# Architecture isolation lift — design

> **Histórico.** Estado atual do contrato: `docs/guides/architecture.md`.

**Data:** 2026-10-24  
**Norte:** diagrama discovery — Widgets → Widget SDK + Ts UI Kit → Application Core → Hardware Interfaces (Ports) → Boards.

## Princípio

Dependências apontam para **interfaces**, nunca para implementações de placa. Widgets não conhecem a placa; a placa não conhece widgets.

## Camadas (lab ↔ discovery)

| Discovery | Lab hoje | Destino deste lift |
|-----------|----------|--------------------|
| Widgets | `firmware/widgets/*` | Sem mudança de pasta; manifests com flags (`hero_only`) |
| Widget SDK | `widget_sdk` + `widget_runtime` | Registry expõe flags; shell abre detalhe só se `!hero_only` |
| Ts UI Kit | `ui_kit` (`kit::`) | Aliases `tessera::ui::Ts*` em cima de `kit::` |
| Application Core | `dashboard_shell` (nav, home, controller) | Nav genérico `Detail` + `widget_id`; controller sem switch de IDs |
| Ports | `hal::DisplayPort` / `TouchPort` / … | + Backlight, Power, Capabilities, Storage; Input = Touch |
| Boards | `boards/jc3248w535` | Adapter implementa os ports; único lugar com pinagem |

## Lacunas que este lift fecha

1. **Controller** ainda faz `strcmp` por id (todoist/weather/…) e trata greeting/date à mão.
2. **Nav** tem `Screen::Todoist|Weather|…` — conhecimento de produto no shell.
3. **Manifest** não declara hero-only / detail.
4. **Ports** misturam backlight em Display; bateria e storage fora do HAL.
5. **Ts UI Kit** existe como `kit::`, sem API `Ts*` do contrato Terrara.
6. **show_widget** faz `unmount_all` + `mount` em vez de `mount_exclusive`.

## Ainda aberto (lab — fechar sem “promoção” como desculpa)

- `list_prefs` / `pending_closes` via `StoragePort`.
- Portal: settings de widget namespaced (`widget_settings` + `/api/widget_settings`).
- Todoist/AI Tokens: rebuild puro no slot (attach reconstrói; sem park/reparent longo).
- `tessera::ui::Ts*` no settings hub e na home.

## Produto discovery ainda não no lab

- Spotify / Calendar / OTA ESP-IDF — widgets/features novos, não só lift.

## Contrato de abertura de widget

```
on_open_widget(id):
  if !registered(id) → ignore
  if hero_only(id) → ignore
  nav.open_detail(id)
  ui.show_widget(id)   // mount_exclusive; widget pinta no mount
```

## Verificação

- `make test` (native)
- `make e2e` / `verify_all` (sim)
- `make flash` + smoke USB (físico)
