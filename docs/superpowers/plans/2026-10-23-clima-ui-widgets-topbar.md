# Clima UI, widgets e topbar — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Autocomplete limpa ao salvar; tiles com ciclo vazio/carregando/pronto/falha; detalhe Clima em lista + agora no topbar (layout C); topbars Todoist/Clima no mesmo helper; ícones sem fundo legíveis em light/dark.

**Architecture:** `kit::subsys_bar` padroniza a barra; `weather_icons` passa a RGB565A8 (alpha 0 no fundo) e `apply_theme()` recalcula cores dos glifos; `weather::View` ganha `failed`; home e weather_page consomem os estados; portal só ajusta o JS do suggest.

**Tech Stack:** LVGL 9, PlatformIO/Arduino ESP32-S3, Open-Meteo (já em HTTP no device), página embutida em `web_page.h`.

## Global Constraints

- Landscape 480×320; tipografia/cores só via `theme` / `palette`.
- Sem HTTPS no portal; GPS LAN fora de escopo.
- Sem segundo banco de bitmaps light/dark — um set + cores por `palette::mode()`.
- Commits só se o usuário pedir; flash com `./tools/flash.sh` ao validar no aparelho.
- Teste nativo: `pio test -e native` onde houver mudança de domínio; UI validada no device.

## File map

| File | Role |
|------|------|
| `src/ui/kit.h` / `kit.cpp` | `subsys_bar(...)` — ←, título, status, ação opcional |
| `src/ui/weather_icons.*` | Bitmaps RGB565A8, tema, API `apply_theme` |
| `src/weather/cache.*` | Flag `failed` + `note_failure()` |
| `src/ui/home.cpp` | Tile clima: spinner/texto por estado |
| `src/ui/weather_page.cpp` | Layout C + topbar via kit |
| `src/ui/shell.cpp` | Todoist header via `subsys_bar` |
| `src/app/controller.cpp` | Chamar `note_failure` no forecast/geocode fail |
| `src/services/web_page.h` | Limpar suggest + cancelar timer no select |

---

### Task 1: Portal — limpar suggest ao selecionar

**Files:**
- Modify: `src/services/web_page.h` (handlers `drawSuggest` / onclick / `suggestCity`)

**Interfaces:**
- Produces: ao clicar sugestão, lista vazia+hidden e `clearTimeout(suggestTimer)` antes do `post`

- [ ] **Step 1: Ajustar o onclick da sugestão**

No `b.onclick` de `drawSuggest`, após setar `weatherPick` e o input:

```javascript
clearTimeout(suggestTimer);box.innerHTML='';box.hidden=true;
post('/api/weather',{name:row.name,latitude:row.latitude,longitude:row.longitude},'Local salvo');
```

(Remover a linha duplicada `box.hidden=true` antiga se sobrar.)

- [ ] **Step 2: Smoke no browser**

Abrir `http://<ip>/`, página Clima, digitar “Camp”, clicar uma linha. Esperado: lista some; toast “Local salvo”; note passa a “Aguardando…” até o painel ficar ready.

- [ ] **Step 3: Commit** (só se o usuário pedir)

---

### Task 2: Cache — estado `failed`

**Files:**
- Modify: `src/weather/cache.h`, `src/weather/cache.cpp`
- Modify: `src/app/controller.cpp` (`on_weather`)
- Test: `test/test_native/` — adicionar ou estender teste de cache se já existir; senão skip nativo e validar via state UI

**Interfaces:**
- Produces: `View.failed`; `void note_failure()`; `set_place` / `store` limpam `failed`

- [ ] **Step 1: Estender a View e a State**

Em `cache.h`:

```cpp
struct View {
    bool placed;
    bool ready;
    bool stale;
    bool failed;
    char name[48];
    Forecast forecast;
};

void note_failure();
```

Em `cache.cpp`: `state.failed`; `set_place` e `store` fazem `failed = false`; `note_failure()` seta `failed = true` se `placed`; `view()` copia `failed`.

- [ ] **Step 2: Controller**

Em `on_weather`, nos ramos `!result.ok` (geocode e forecast), chamar `weather::note_failure()` antes de `paint_weather` / `publish` (geocode: após set_status).

- [ ] **Step 3: Compilar**

Run: `cd todoist-panel && pio run -e jc3248w535`  
Expected: SUCCESS

---

### Task 3: `kit::subsys_bar`

**Files:**
- Modify: `src/ui/kit.h`, `src/ui/kit.cpp`

**Interfaces:**
- Produces:

```cpp
struct SubsysBar {
    lv_obj_t *root;
    lv_obj_t *title;
    lv_obj_t *status;  // may be hidden
    lv_obj_t *action;  // nullptr if none
};

SubsysBar subsys_bar(lv_obj_t *parent, const char *back_symbol, lv_event_cb_t on_back, uint32_t tone,
                     lv_event_cb_t on_action, const char *action_symbol);
```

- [ ] **Step 1: Implementar**

Padding: left/right 8, ver 6, column 10. Row flex start/center.  
`icon_button(root, back_symbol, on_back, tone)`.  
Coluna flex-grow com `title` (`theme::title_font`, `text_primary`) e `status` (`small_font`, `muted`, LONG_DOT, width 100%, começa hidden).  
Se `on_action && action_symbol`: `icon_button` à direita; senão `action = nullptr`.

- [ ] **Step 2: Compilar** — `pio run -e jc3248w535` SUCCESS

---

### Task 4: Shell Todoist usa `subsys_bar`

**Files:**
- Modify: `src/ui/shell.cpp` (`build_header`, `announce`, `apply_theme`)

**Interfaces:**
- Consumes: `kit::subsys_bar`
- Keep globals `title_label` / `status_label` apontando para `bar.title` / `bar.status` (ou renomear e atualizar usos)

- [ ] **Step 1: Trocar `build_header`**

```cpp
kit::SubsysBar bar = kit::subsys_bar(screen, LV_SYMBOL_LEFT, on_back, palette::widget_todoist(),
                                     on_refresh, LV_SYMBOL_REFRESH);
header = bar.root;
title_label = bar.title;
status_label = bar.status;
lv_obj_add_flag(title_label, LV_OBJ_FLAG_HIDDEN);  // comportamento atual
```

Remover montagem manual duplicada.

- [ ] **Step 2: Flash sanity** — abrir Todoist: ←, refresh e status “carregando”/“falhou” intactos.

---

### Task 5: Ícones RGB565A8 + tema

**Files:**
- Modify: `src/ui/weather_icons.h`, `src/ui/weather_icons.cpp`
- Call site: `ui::apply_theme` → `weather_icons::apply_theme()`

**Interfaces:**
- Produces: `void apply_theme()`; `for_code` / `unavailable` inalterados na assinatura
- Format: `LV_COLOR_FORMAT_RGB565A8` — buffer `uint8_t` com stride `w*3` (RGB565 + A8 por pixel) **ou** layout que o LVGL 9 do projeto documentar; fundo A=0, glifo A=255

- [ ] **Step 1: Helpers**

Substituir `clear(..., bg)` por clear alpha 0.  
`put(x,y,rgb565)` escreve cor + A=255.  
Cores dos glifos: se `palette::mode() == Dark`, sol/nuvem mais claros; se Light, tinta um pouco mais escura (contraste no paper).

- [ ] **Step 2: `apply_theme`**

Rebuild estáticos (`clear_sky = make_clear()` …) e `lv_image_cache_drop` nos dsc.

- [ ] **Step 3: Hook**

Em `ui::apply_theme` / `weather_page::apply_theme` / `home::apply_theme`, chamar `weather_icons::apply_theme()` uma vez (de `ui::apply_theme` basta).

- [ ] **Step 4: Visual** — ícone sem retângulo escuro na home e na lista.

---

### Task 6: Home — ciclo de vida do tile Clima

**Files:**
- Modify: `src/ui/home.cpp` (`set_weather`, begin do tile)

**Interfaces:**
- Consumes: `View.failed`, `kit::spinner`, `weather_icons::*`

- [ ] **Step 1: Slot do ícone**

Manter `weather_glyph`. Adicionar `weather_spinner` (filho do mesmo slot, hidden por padrão, `kit::spinner(slot, 28, 5)`).

- [ ] **Step 2: `set_weather`**

```text
!placed          → detail "definir local"; glyph unavailable; hide spinner; show glyph
placed && failed && !ready → "falhou"; unavailable; hide spinner
placed && !ready → "carregando…"; hide glyph; show spinner
ready && stale   → "N °C · frase · desatualizado"; glyph; hide spinner
ready            → "N °C · frase"; glyph; hide spinner
```

- [ ] **Step 3: Todoist tile** — se `pending` em `set_summary`, texto “…” já existe; opcional spinner só se não atrapalhar o número (YAGNI: manter “…”).

---

### Task 7: Weather page — layout C + topbar

**Files:**
- Rewrite body of: `src/ui/weather_page.cpp`
- Modify: `src/ui/weather_page.h` se precisar expor nada novo

**Interfaces:**
- Consumes: `kit::subsys_bar`, `weather_icons`, `weather::phrase`

- [ ] **Step 1: Header**

`kit::subsys_bar(screen, LV_SYMBOL_LEFT, on_back, palette::widget_weather(), nullptr, nullptr)`.  
`title` = nome do local ou “Clima”.  
`status` = `22° · Nublado` / `carregando…` / `definir local` / `falhou`.

- [ ] **Step 2: Lista**

Remover hero (ícone grande, temp display, range).  
`lv_obj_clean` + para cada dia `0..6` (ou `1..6` se Hoje for só no status — **spec C:** lista inclui Hoje):

Linha flex row, width 100%, pad ver 8, border bottom 1px `theme::border()`:
- label dia (`Hoje` / `Amanhã` / weekday curto a partir da data ISO)
- `lv_image` 32px
- label `máx°  mín°` alinhado à direita (`flex_grow` spacer antes, ou space-between)

Weekday: implementar `weekday_pt` de verdade (algoritmo civil simples a partir de YYYY-MM-DD) — labels `SEG`…`DOM`.

- [ ] **Step 3: `paint` / `apply_theme`**

Atualizar cores via theme; chamar ícones já tematizados.

- [ ] **Step 4: Flash** — detalhe sem cards wrap; topbar alinhado ao Todoist.

---

### Task 8: Integração e verificação no aparelho

- [ ] **Step 1:** `./tools/flash.sh`
- [ ] **Step 2:** Web: selecionar cidade → suggest some → tile “carregando…” → temp
- [ ] **Step 3:** Abrir Clima: lista + status no topbar; trocar tema light/dark: ícones ok sem fundo
- [ ] **Step 4:** Todoist: topbar igual em medidas; refresh ok

---

## Spec coverage

| Spec § | Task |
|--------|------|
| 1 Web suggest clear + save | 1 |
| 2 Widget lifecycle | 2, 6 |
| 3 Detail layout C | 7 |
| 4 Topbar padrao | 3, 4, 7 |
| 5 Icons no bg + theme | 5 |
| Aceite 1–5 | 8 |

## Placeholder / consistency self-check

- Nomes: `subsys_bar`, `note_failure`, `apply_theme` estáveis entre tasks.
- Sem TBD; RGB565A8 escolhido explicitamente (se o link LVGL falhar no device, fallback documentado na Task 5: chroma magenta + não desenhar A=0 via conversão no `finish`).
