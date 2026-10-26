# Google Calendar (bridge agent)

**Substitui** a intenção de OAuth no device em `2026-10-23-calendario-design.md`.

## Decisões

- **Auth:** Tessera Agent no Mac (OAuth Desktop + refresh longo em `~/.tessera/calendar/`).
- **Device:** sem tokens Google; só snapshot USB + seleção NVS.
- **Multi-conta:** N contas no agent; portal do device escolhe calendários por conta.
- **Adicionar conta:** portal **ou Agente** → `CALENDAR_AUTH?` / `/api/calendar/add_account` → browser no Mac.
- **UI v1:** tile home + detalhe **só de hoje** + estados (carregando / vazio / offline). Somente leitura.
- **Escopo:** somente compromissos do dia corrente — sem amanhã em diante (agentd busca janela [00:00, amanhã); firmware ignora `day_offset != 0`).
- **Dia todo:** só eventos all-day de **um único dia = hoje** (Google: `start.date == hoje` e fim exclusivo = amanhã). Multi-dia não entra.
- **Com hora:** início e fim no dia de hoje; já terminados saem da lista.
- **Ordem:** **Dia todo** primeiro; depois horários crescentes (início). Se não houver hoje → “livre” / “Nada hoje”.
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
| `CALENDAR\tv1\tbridge\tlinked\tevents…` | Mac→painel | snapshot **só hoje** (`day_offset=0`; campo mantido por compat) |

## Portal

Página **Agenda**: status, Adicionar conta, checkboxes por calendário, `/api/state.calendar`.

Setup Mac: `~/.tessera/calendar/client.json` (OAuth Desktop do Google Cloud).

## Widget

Id `org.tessera.calendar`. Parse/cache em `firmware/widgets/calendar/`. Ícones Material Symbols (Apache 2.0).
