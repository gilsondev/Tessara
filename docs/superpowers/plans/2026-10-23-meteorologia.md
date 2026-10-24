# Plano do Clima (alinhamento pós-grill)

> Spec: `docs/superpowers/specs/2026-10-23-meteorologia-design.md`. Glossário: `GLOSSARY.md`. ADR-0001.

**Objetivo:** alinhar o firmware já iniciado ao modelo fechado: cartão com ícone, tela diária (6 dias), Local via cidade + geolocalização na página do celular, linha Clima nos ajustes do aparelho.

**Teste:** `pio test -e native` após etapas de domínio; flash + probe visual no aparelho no fim.

## Etapa A — Parse e cache diários

Arquivos: `src/weather/parse.*`, `src/weather/cache.*` (só se a View mudar), `src/services/weather_client.cpp`, `test/test_native/test_weather_parse.cpp`.

- `Forecast` traz atual + `Day days[7]` (code, temp_min_c, temp_max_c). Remove a obrigatoriedade das 12 horas.
- `parse_forecast` exige `current` e `daily` com ≥7 entradas de code/min/max.
- URL do forecast usa `daily=weather_code,temperature_2m_max,temperature_2m_min&forecast_days=7`.
- Testes nativos atualizados; suite verde.

## Etapa B — Ícones de condição

Arquivos: `src/ui/weather_icons.h`, `src/ui/weather_icons.cpp` (ou equivalente).

- Mapa código WMO → bitmap (agrupado pelas frases do spec) + bitmap genérico “indisponível”.
- API: `const lv_image_dsc_t *weather_icons::for_code(int)` e `unavailable()`.

## Etapa C — Cartão e Tela do Clima

Arquivos: `src/ui/home.cpp`, `src/ui/weather_page.cpp`.

- Cartão: ícone da condição (ou genérico) + texto conforme estados do spec.
- Tela: bloco central (ícone grande, min/max de hoje, condição); grade dos próximos 6 dias; Open-Meteo.

## Etapa D — Página do celular e aparelho

Arquivos: `src/services/web_page.h`, `src/services/web_portal.cpp`, `src/ui/settings_panel.cpp`.

- Página: botão “Usar minha localização” (Geolocation API) + campo cidade; `POST /api/weather` aceita `{city}` **ou** `{latitude,longitude,name?}`.
- Ajustes no aparelho: linha Clima no hub + QR (Local só no celular).

## Pronto

Testes nativos verdes. No aparelho: cartão com ícone, tela com 6 dias, Local pela página (cidade ou GPS do browser), Wi‑Fi off mantém “desatualizado”.
