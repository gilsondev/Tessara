## Promoção efetuada

Copiado do lab `esp32-firmwares/todoist-panel` em 2026-10-24 para este repositório. MVP Tessara (clock/weather/shell/SDK) + extras incubados (Todoist, AI Tokens, agent, screen rest).

# Promoção Labs → repositório Tessara

## Estratégia (owner)

Este POC **é o lugar onde amadurecemos** a forma Tessara (shell, widgets, sim, web, agent, finish gate). Quando estiver maduro o bastante, **copiamos / extraímos** para um repositório Tessara novo (ou Terrara `firmware/`), em vez de reimplementar do zero.

As features já feitas (mesmo além do MVP inicial) **são boas e ficam**. O caminho é **lift** delas para o Widget SDK / runtime / settings namespaced — ver `architecture-lift.md` — e só então promover. Novos widgets nascem desacoplados, independentes e configuráveis em cima desse contrato.

Isso **não** anula ADR-009 no Terrara: a verdade de produto/MVP continua em `Terrara/docs/specs/`. O lab é **trunk de incubação** até a promoção documentada.

## O que promover ( aceitável no trunk Tessara )

| Camada | Critério de madureza |
|---|---|
| Layout `firmware/components|widgets|boards` | Espelha ARCHITECTURE; includes allowlist limpa |
| `widget_sdk` / `widget_runtime` | Mount/unmount + generation; WidgetContext mínimo estável |
| `ui_kit` + `dashboard_shell` | Home fina; settings device vs widget; sem footer obrigatório |
| Simulador | Mesmas fontes de UI; fixtures de falha; `make e2e` verde |
| Weather (+ clock detalhe) | MVP Tessara: clock + weather de produção |
| Web configurator | Device hub + páginas por widget; QR / mDNS |
| Finish gate | `make test` → `e2e` → `flash` → `device` documentado |
| Hello / allowlist | Prova de 3º widget sem editar o core |

## O que **não** entra no MVP Tessara (fica lab ou widget opcional)

- Todoist como requisito de aceite (pode ir como widget opcional depois)
- Calendar / Notebook (só prototype HTML no Terrara)
- Arduino_GFX / `controller` monolítico como **arquitetura final** — ao promover, preferir ports + caminho ESP-IDF (ou ADR explícito se ainda Arduino)
- Secrets, CAD `hardware/stand`, binários `.pio`, NVS de máquina
- Branding “Tessara” residual → renomear para **Tessara** na cópia

## Checklist antes de criar o repo Tessara

1. [ ] `make finish-host` verde; screenshots revisados vs design
2. [ ] Clock + weather no sim e no device; estados stale/offline/unsynced rotulados
3. [ ] Scenarios SIMULATOR.md cobertos (ou backlog explícito no PR de promoção)
4. [ ] Allowlist CI-equivalente (`test_widget_allowlist`) sem vazamento board/Wi‑Fi em widgets
5. [ ] Runtime: unmount + request em voo não toca LVGL (teste nativo)
6. [ ] Doc de fronteiras + mapa “lab path → Tessara path”
7. [ ] ADR no Terrara: lab incubou; promoção supersede/atualiza ADR-009
8. [ ] Cópia limpa: sem tokens, sem `hardware/stand` a menos que o repo de produto queira CAD separado

## Como continuar **aqui** (regra de trabalho)

1. Toda feature nova deve ser **Tessara-shaped**: pacote em `widgets/` ou `components/`, testável no host, exercitável no sim.
2. Preferir clock/weather/SDK/sim faults sobre mais superfície Todoist — Todoist só se validar padrão de widget.
3. Finish gate obrigatório (`AGENTS.md` / `make help`).
4. Ao divergir das specs Terrara, anotar em `docs/` do lab **e** refletir no Terrara na promoção.

Specs de referência (outra pasta):

- `~/Developer/Projects/gilsondev/Terrara/docs/specs/ARCHITECTURE.md`
- `…/SIMULATOR.md`, `WIDGET-SDK.md`, `DEVICE-EXPERIENCE.md`, `DELIVERY.md`, `PRODUCT.md`
- `…/adr/0009-lab-spike-is-reference-not-trunk.md` (atualizar na promoção)
