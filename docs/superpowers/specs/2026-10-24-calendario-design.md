# Google Calendar (bridge agent)

**Substitui** a intenção de OAuth no device em `2026-10-23-calendario-design.md`.

## Decisões

- **Auth:** Tessera Agent no Mac (OAuth Desktop + refresh longo em `~/.tessera/calendar/`).
- **Device:** sem tokens Google; só snapshot USB + seleção NVS.
- **Multi-conta:** N contas no agent; portal do device escolhe calendários por conta.
- **Adicionar conta:** portal **ou Agente** → `CALENDAR_AUTH?` / `/api/calendar/add_account` → browser no Mac.
- **UI v1:** tile home + detalhe com **hoje e os próximos dias** + estados (carregando / vazio / offline). Somente leitura.
- **Escopo (revisado em 2026-10-26):** hoje **e os próximos dias** — agentd busca a janela `[hoje 00:00, +8 dias)` e marca `day_offset`; o firmware agrupa por dia. O escopo antigo era só o dia corrente, o que deixava o painel em "livre" todas as noites, depois do último compromisso. Ver [slide/calendário](2026-10-26-calendar-upcoming-days-design.md).
- **Dia todo:** só eventos all-day de **um único dia** (Google: fim exclusivo = dia seguinte ao início). Multi-dia não entra.
- **Com hora:** `day_offset` vem do dia de início; já terminados saem da lista.
- **Ordem:** dia mais próximo primeiro; dentro do dia, **Dia todo** antes dos horários crescentes. Sem nada hoje → status “livre hoje” e o próximo dia assume a lista; sem nada na janela → “Agenda livre”.
- Agente (Widgets → Agenda): mesmas contas/calendários que o portal (marcar/desmarcar, adicionar conta).
- Fora do aceite MVP (como Todoist).

## Protocolo USB

| Linha | Direção | Papel |
|---|---|---|
| `CALENDAR_AUTH?` | painel→Mac | inicia OAuth |
| `CALENDAR_AUTH\tv1\t0\|1\tstatus` | Mac→painel | pending / done |
| `CALENDAR_CATALOG?\t…` | painel→Mac | pedir catálogo |
| `CALENDAR_CATALOG\tv1\t…` | Mac→painel | contas + calendários |
| `CALENDAR_SEL\tid…` | painel→Mac | seleção |
| `CALENDAR?` | painel→Mac | pedir eventos |
| `CALENDAR\tv1\tbridge\tlinked\tevents…` | Mac→painel | snapshot de hoje **e dos próximos dias** (`day_offset` = 0 hoje, 1 amanhã, …) |

## Portal

Página **Agenda**: status, Adicionar conta, checkboxes por calendário, `/api/state.calendar`.

Setup Mac: `~/.tessera/calendar/client.json` (OAuth Desktop do Google Cloud).

## Widget

Id `org.tessera.calendar`. Parse/cache em `firmware/widgets/calendar/`. Ícones Material Symbols (Apache 2.0).
