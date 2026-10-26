# Calendário: hoje e os próximos dias

**Data:** 2026-10-26
**Status:** implementado e verificado no JC3248
**Revisa:** [`2026-10-24-calendario-design.md`](2026-10-24-calendario-design.md) (escopo "só hoje")
**Arquivos:** `caps-calendar/src/{fetch.rs,lib.rs}`, `agentd/src/logutil.rs`,
`firmware/widgets/calendar/`, `dashboard_shell/ui/home.cpp`

## Problema

Relato: "o calendário parou de mostrar compromissos no tile e na página de detalhes".

Não era regressão. A agenda do Mac respondia normalmente; o único compromisso do dia
(`Pets: Agua e Ração`, 17:00–17:30) tinha acabado, e o escopo documentado era **só o dia corrente**,
descartando o que já terminou:

- `fetch.rs` buscava a janela `[hoje 00:00, amanhã 00:00)` e aplicava `if e <= now { return None }`;
- `lib.rs` filtrava `day_offset == 0` ao codificar a linha e ao ler o cache;
- o firmware filtrava `day_offset == 0` no tile, nos cartões e na contagem de status.

Resultado: toda noite, depois do último compromisso, o painel ficava em "livre" até a meia-noite —
mesmo com compromissos no dia seguinte. Para quem olha o painel à noite, isso parece defeito.

## Decisão

Mostrar **hoje e, quando hoje já acabou, o próximo dia com eventos**. O protocolo USB não mudou: o
campo `day_offset` já existia na linha `CALENDAR` (o parser do firmware sempre aceitou 0 a 14) e só
estava sendo preenchido com zero.

**agentd**

- Janela de busca `[hoje 00:00, +8 dias)` (`WINDOW_DAYS`); `maxResults` por calendário 8 → 12.
- `day_offset` calculado do dia de início; fora de 0..14 o evento é descartado.
- "Já terminou" continua valendo (compara com `now`), então hoje nunca mostra passado.
- All-day: segue só de um único dia, mas agora em qualquer dia da janela (fim exclusivo = dia
  seguinte ao início), o que inclui aniversários e feriados futuros.
- Ordem: dia mais próximo primeiro; dentro do dia, Dia todo antes dos horários.
- O cache em disco guarda offsets relativos ao dia em que foi escrito. Um Mac que dormiu depois da
  meia-noite rotularia hoje como "amanhã", então `load_cached_events` desloca os offsets por
  `cache_age_in_days(fetched_at)` e descarta os que ficaram negativos.

**Firmware** (helpers testáveis em `calendar/snapshot.cpp`, usados pelo tile e pela página)

- `next_day(view)` — dia mais próximo com eventos, ou -1.
- `events_on_day(view, dia, out, cap)` — índices do dia, Dia todo primeiro.
- `day_label` — "Hoje · 2", "Amanhã", "Quinta · 1"; sem relógio, "Em 3 dias".
- `day_prefix` — forma curta para o tile: vazio hoje, "Amanhã", depois "Qui".
- A página monta **um cartão por dia** com eventos, e o resumo do topo mostra o próximo
  ("Próximo hoje" ou "Próximo · Amanhã").
- Status do cabeçalho: contagem de hoje; sem nada hoje mas com dias à frente, **"livre hoje"**;
  nada na janela inteira, "livre" e o estado vazio "Nada pelos próximos dias.".

O nome do dia da semana precisa do relógio. A página é um widget e o Widget SDK só expõe tempo
monotônico, então `CalendarWidget::mount` passa `context.time()->now().wday` para
`calendar_page::set_weekday`; o tile da home usa `daytime::local`. Sem relógio sincronizado, os
rótulos caem para "Em N dias" / "+Nd" em vez de inventar um dia.

## Resultado no device

Tile da home (noite de segunda, nada mais hoje):

```
Calendário
Amanhã dia todo · Aniversário Selma
Amanhã 09:15 · Estudos Bíblicos: Devocional
```

Página de detalhes: status "livre hoje", resumo "Próximo · Amanhã", cartão "Amanhã · 3" e, abaixo,
"Quarta · 4". O agentd passou de `calendar refresh ok (0 eventos)` para `(12 eventos)`.

O log do USB também contava errado: mostrava "3 evento(s)" porque imprimia no máximo três. Agora
conta todos e marca o dia dos que imprime (`4 evento(s): amanhã Dia todo · … | +2d 17:00 · …`).

## Verificação

- `make test` — 116 nativos, incluindo `test_calendar_next_day_falls_forward` (noite sem nada hoje),
  `test_calendar_day_labels` e `test_calendar_no_events_has_no_next_day`
- `cargo test -p tessera-caps-calendar -p agentd` — codificação com `day_offset != 0`, idade do
  cache, resumo do log
- `make finish-host`, `make flash`, `make agent-install` e conferência nas duas telas

## O que ficou de fora

- Janela configurável (hoje é fixa em 8 dias) e limite de 12 eventos no fio.
- Multi-dia (eventos all-day que atravessam dias) continua fora.
- Rolagem da página pode ficar longa com a semana cheia; não há "ver mais".
