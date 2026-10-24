# Lift: features boas → arquitetura Tessara

## Decisão (owner)

As funcionalidades já feitas (Todoist, clima, AI Tokens, settings, screen rest, agent, web, sim…) **ficam**. Passar um pouco do MVP inicial é ok.

O trabalho seguinte não é jogar fora o POC: é **refatorar essas features para rodarem em cima da arquitetura** de `Terrara/docs/specs/` (ARCHITECTURE, WIDGET-SDK, SIMULATOR). A partir daí, **novos widgets** nascem desacoplados, independentes e configuráveis.

## Estado atual (pós L1–L7 + isolation lift no lab)

Já existe:

- `widget_sdk` — `TsWidget` / `WidgetManifest` (`hero_only`) / `WidgetContext` com **time / settings / http / scheduler / logger** + `MemoryHost`
- `widget_runtime` — registry (id+title+hero_only) + `mount_exclusive` + generation cancel
- Pacotes em `firmware/widgets/{weather,todoist,aitokens,clock,greeting,date}`; greeting/date = `hero_only`
- Nav genérico: `Screen::{Home,Detail,Settings}` + `widget_id` — shell não tem telas por produto
- Controller abre detalhe só via registry (`type_registered` / `type_hero_only`)
- Ports HAL: Display, Input(=Touch), Backlight, Power, Sound, Button, Storage, Capabilities — board JC adapter
- Ts UI Kit: `ui/ts_kit.h` aliases `tessara::ui::Ts*` sobre `kit::`
- Home abre widgets por **id**; compose ∩ registry
- Sim faults + `verify_all`

Contrato Tessara (lab):

- `DeviceHttp` + `net_worker::HttpGet` — widgets pedem GET via `ctx.http()`; unmount cancela
- `detail_host` — `ui::show_widget` → `mount_exclusive(id, slot)`
- Weather: `forecast_url` / `accept_http_forecast` testáveis no host; mount dispara GET

Fechado no lab (isolation lift):

- `list_prefs` / `pending_closes` → `StoragePort`
- Settings de widget no portal namespaced (`/api/widget_settings` + NVS `w_*`)
- AI Tokens/Todoist rebuild puro no slot (sem reparent de árvore longa)
- `tessara::ui::Ts*` no settings hub e na home

## Norte (contrato Tessara)

Cada widget é um **pacote** que:

1. Declara manifest (`id`, título, tamanho, accent, schema de settings)
2. Implementa `mount` / `on_resize` / `unmount` no slot do shell
3. Pede dados só por **WidgetContext** (time / http / settings / scheduler / logger)
4. Tem **summary** (home) + **detail** opcional no mesmo pacote
5. Settings **namespaced** por instância no web + painel (device vs widget)
6. Compila no host e roda no sim **sem** headers de board/Wi‑Fi
7. Registra-se com **uma** entrada no registry/build — sem editar outros widgets

O shell só: slots, navegação, tema global, status (Wi‑Fi/hora). Não conhece Todoist/Clima/…

## Fases de lift (ordem)

| Fase | Objetivo | Saída |
|---:|---|---|
| **L0** | Congelar UI/comportamento atual com `make finish-host` + `make device` verdes | Baseline de regressão |
| **L1** | Completar `WidgetContext` (time, settings get/observe, http job stub, scheduler, logger) + testes nativos de generation/cancel | SDK usável |
| **L2** | Weather 100% via runtime + ctx (summary home + detail); controller só despacha | 1º widget “puro” |
| **L3** | Clock detalhe + estados synced/unsynced; hero do home continua no shell | MVP Tessara no contrato |
| **L4** | AI Tokens via runtime + settings de widget; agent continua core_services | 2º widget desacoplado |
| **L5** | Todoist via mesmo contrato (padrão de widget mutável); fora do aceite MVP produto | Prova de extensão |
| **L6** | Shell fino: tirar conhecimento de IDs específicos; compose só por registry | Novos widgets = só pacote + register |
| **L7** | Sim faults (SIMULATOR.md) + allowlist no e2e | Pronto para promoção |

Não bloquear L2–L5 em ESP-IDF: a forma SDK vem primeiro; ports/IDF entram na promoção ou em paralelo sem quebrar widgets.

## Regra para features novas

- **Antes** do lift do tipo afetado: pode entregar valor, mas no pacote `widgets/X` e com caminho claro para ctx.
- **Depois** do lift do tipo: proibido novo acoplamento no `controller` para esse widget.
- Novo widget (Calendar, etc.): **só** depois de L1+L2 (ou no mínimo L1 + clock como template).

## Relação com promoção

Ver `promotion-to-tessara.md`. Lift aqui = deixar a árvore **copiável** para o repo Tessara sem reescrever as features.
