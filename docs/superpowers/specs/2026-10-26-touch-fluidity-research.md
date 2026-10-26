# Pesquisa: toque e efeitos no nível de smartphone (Tessera / LVGL)

**Data:** 2026-10-26  
**Contexto:** JC3248 + LVGL 9 no firmware Tessera.

## Orçamento de frame

- 60 Hz → **≤16 ms** por frame (build + raster); 120 Hz → ≤8 ms.
- Jank = frame > orçamento → animação “engasga”. No ESP32: DMA + double buffer + LVGL em core dedicado quando possível; evitar trabalho pesado em `LV_EVENT_*` (só estado; física num timer).

## Feedback de press (o que o dedo espera)

1. **Press visual ≤1 frame:** escurecer / `transform_scale` ~0.96–0.98 no `PRESSED`; reverter no `RELEASED` com transition 80–120 ms (`ease_out`).
2. **Alvo ≥44×44 pt** (HIG); Tessera já usa `ext_click_area` em alguns botões — padronizar no UI Kit.
3. **Tap ≠ drag:** tolerância de movimento (~8–12 px) antes de cancelar click (padrão Apple Watch / Flutter).
4. **Haptic (opcional):** disparar no indev `read_cb` ou `feedback_cb` do LVGL no instante do hit confirmado — vibração curta (~10 ms), não em todo `PRESSING`.

## Scroll estilo telefone

| Comportamento | iOS-like | LVGL nativo | Ação Tessera |
|---|---|---|---|
| Momentum | platô de velocidade + decay | decay imediato (ease_out) | custom `anim_path` ou `scroll_to` com path próprio ([lvgl#5074](https://github.com/lvgl/lvgl/issues/5074)) |
| Overscroll | rubber-band | elastic disponível | ligar elastic + snap onde fizer sentido |
| Encadeamento | parent/child | scroll chaining | revisar listas (calendar/weather) |
| Hit em lista | CLICKABLE no scroller | kit::bare remove click | já documentado em calendar_page |

## Pipeline de input

- Preferir **event-mode** no touch IC (IRQ) em vez de só poll lento.
- Filtrar jitter (média móvel leve) sem matar latência (<5 ms).
- Não bloquear o loop LVGL com USB/rede no mesmo tick do gesture.

## Efeitos de UI

- Transitions de estilo em state change (pressed/checked) — LVGL styles + `transition`.
- Evitar full-screen invalidate; dirty regions / widgets opacos (`declare_opaque`).
- Animações de página: shared element / slide 200–280 ms `ease_out`, não linear.

## Roadmap sugerido (próximo corte firmware)

1. UI Kit: press scale + transition padrão em `icon_button` / tiles.
2. Listas: elastic scroll + tuning de throw; testar no **touch real** (mouse no sim mente).
3. Medir FPS/`PERF` no device durante fling e press.
4. (Opcional) haptic driver na JC3248 se o hardware tiver motor.

## Fontes

- LVGL Features (scroll momentum, style transitions, transforms)
- LVGL #5074 / #7493 (scroll prediction / inertia)
- LVGL blog Apple Watch bubbles (press + timer physics)
- Flutter perf / Cupertino scroll physics / HIG 44pt
