# Google Calendar (bridge agent)

**Substitui** a intenção de OAuth no device em `2026-10-23-calendario-design.md`.

## Decisões

- **Auth:** Tessara Agent no Mac (OAuth Desktop + refresh longo em `~/.tessera/calendar/`).
- **Device:** sem tokens Google; só snapshot USB + seleção NVS.
- **Multi-conta:** N contas no agent; portal do device escolhe calendários por conta.
- **Adicionar conta:** portal → `CALENDAR_AUTH?` → browser no Mac.
- **UI v1:** tile home + lista de hoje + estados (carregando / vazio / offline). Somente leitura.
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
| `CALENDAR\tv1\tbridge\tlinked\tevents…` | Mac→painel | snapshot (hoje + próximos dias; cada evento termina com `day_offset`) |

## Portal

Página **Agenda**: status, Adicionar conta, checkboxes por calendário, `/api/state.calendar`.

Setup Mac: `~/.tessera/calendar/client.json` (OAuth Desktop do Google Cloud).

## Widget

Id `org.tessara.calendar`. Parse/cache em `firmware/widgets/calendar/`. Ícones Material Symbols (Apache 2.0).
