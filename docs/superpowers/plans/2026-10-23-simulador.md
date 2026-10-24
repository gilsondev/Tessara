# Plano do simulador

> A janela da home começa depois da etapa 4 de `docs/superpowers/plans/2026-10-23-home-widgets.md`. Clima e agenda na janela esperam os planos deles. Spec: `docs/superpowers/specs/2026-10-23-simulador-design.md`.

**Objetivo:** abrir o painel no Mac, com as mesmas telas do aparelho, alimentado por arquivos de cenário.

## Etapa 1 — Cerca de includes

Arquivo: `test/test_native/test_portable_includes.cpp`.

O teste lê os `.h` e `.cpp` de `src/ui`, `src/todoist`, `src/weather` e `src/calendar` e falha se encontrar `Arduino.h`, `WiFi.h`, `Preferences.h` ou `boards/`. Pastas que ainda não existem são ignoradas. Esta etapa só cria o teste e move include proibido que ele apontar na home, se a etapa 4 da home já tiver deixado algum.

## Etapa 2 — Programa SDL

Arquivos: `sim/CMakeLists.txt`, `sim/main.cpp`, `sim/fixtures/normal.json`, `sim/README.md` não entra. O modo de build fica no comentário do `CMakeLists.txt`: LVGL 9.2.2, SDL2, C++17, janela 320×480.

`sim/main.cpp` sobe o display LVGL, carrega o cenário e monta a home. Não chama Wi-Fi nem NVS.

Comando de desenvolvimento:

```bash
cmake -S sim -B sim/build && cmake --build sim/build
sim/build/painel --viewport 320x480 --scenario normal --screenshot /tmp/painel-normal.png
```

O PNG tem 320×480. Isso é a prova desta etapa.

## Etapa 3 — Cenários

Arquivos em `sim/fixtures/`: `sem_bateria.json`, `offline.json`, `sem_local.json`, `sem_agenda.json`.

Cada um produz um PNG pelo mesmo comando, trocando `--scenario`. O teste nativo da cerca continua verde. O cenário `resposta_atrasada` não ganha PNG próprio: ele já é o teste de geração do clima.

## Etapa 4 — Toque e voltar

O clique do mouse no cartão do Todoist abre a tela de tarefas do cenário e o voltar regressa à home. O mesmo vale para clima e agenda quando esses módulos existirem. Até lá, o clique num cartão ausente não faz nada.

Ajustes abrem por um clique na engrenagem e gravam num diretório passado por `--settings /tmp/painel-settings`. O arquivo não é a NVS do aparelho.

## Pronto

`sim/build/painel --screenshot` gera a home do cenário `normal` sem ESP32 ligado. Os testes nativos seguem passando no mesmo dia.
