# Plano do Google Calendar

> Começa depois da etapa 8 de `docs/superpowers/plans/2026-10-23-home-widgets.md`. Pode andar em paralelo com a meteorologia até a home, porque mexe em outros arquivos. Spec: `docs/superpowers/specs/2026-10-23-calendario-design.md`.

**Objetivo:** um cartão da agenda de hoje e uma tela com até 8 compromissos, ligados por OAuth de aparelho.

**Teste:** `pio test -e native` em cada etapa. Nenhum teste imprime ou fixa token real.

## Etapa 1 — Lista de hoje

Arquivos: `src/calendar/parse.h`, `src/calendar/parse.cpp`, `test/test_native/test_calendar_parse.cpp`.

`calendar::parse_events(json, agora, fuso)` produz no máximo 8 itens com `title`, `minutes` (−1 para o dia inteiro) e `all_day`. Evento cujo fim já passou fica de fora. O próximo é o primeiro item. O total é o tamanho da lista.

Fixture: um evento 09:00–10:00, um dia inteiro, um 15:00–16:00, um já encerrado. Com `agora` às 12:00, a lista fica dia inteiro, 15:00, total 2. O encerrado e o das 09:00 ficam de fora.

## Etapa 2 — Sessão

Arquivos: `src/calendar/session.h`, `src/calendar/session.cpp`, `test/test_native/test_calendar_session.cpp`.

- `start_link()` guarda `device_code` e `user_code` só na sessão. `public_code()` devolve o código curto e a URL `https://www.google.com/device`.
- `on_token_http(status, body)` em 200 grava que a conta existe e apaga o código curto da memória pública.
- `on_events_http(401)` uma vez pede renovação. O segundo 401 chama `clear()` e `linked()` fica falso.
- `state_json()` contém `calendar_set` e não contém `refresh`, `access`, `device_code` nem `client_id`.

A gravação real do refresh token na NVS, chave `agenda` do namespace `todoist`, fica na etapa 4. Esta etapa testa a máquina com um armazenador em memória.

## Etapa 3 — Pedidos

Arquivos: `src/services/net_worker.h`, `src/services/net_worker.cpp`, `src/app/controller.cpp`.

`Kind::Calendar` só entra na fila quando `linked()` é verdadeiro e a lista tem mais de 15 minutos ou está vazia. O intervalo é `900000` ms. Fechamento de tarefa do Todoist continua na frente. Durante a ligação, o trabalhador só faz o poll do token no intervalo que o Google devolver, no mínimo 5 s.

O `client_id` é lido de `include/secrets.h` (`GOOGLE_CLIENT_ID`). Ausente, a aba mostra "falta o client id" e não chama o Google.

## Etapa 4 — Cartão, tela e página

Arquivos: `src/ui/home.cpp`, `src/ui/calendar_page.cpp`, `src/services/web_page.h`, `src/services/web_portal.cpp`.

- Cartão com os três textos do spec.
- Tela com até 8 linhas e voltar.
- `POST /api/calendar/link` inicia. `POST /api/calendar/cancel` limpa a ligação em andamento. `DELETE` não é usado.
- `GET /api/state` ganha `calendar_set` e, só enquanto a ligação está aberta, `calendar_code` e `calendar_url`.

## Pronto

Testes nativos verdes. No celular, a ligação mostra o código e, depois de autorizar, o cartão passa a mostrar o próximo compromisso de hoje. Desligar a rede mantém a lista com "desatualizado".
