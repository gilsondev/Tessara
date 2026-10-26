# Calendar widget

**Id:** `org.tessera.calendar`  
**Accent:** `#1A73E8`

Read-only Google Calendar agenda for today. Events arrive via the Tessera Agent USB snapshot (`CALENDAR` lines). Account link and calendar selection live on the device web portal; OAuth tokens stay on the Mac.

## Settings

Namespaced under `org.tessera.calendar` (no tokens on device):

| Key | Values | Default | Notes |
|---|---|---|---|
| `selection` | comma-separated Google calendar ids | empty | Portal Agenda |
| `home_tile_size` | `S` · `M` · `L` · `XL` | `M` | Rows on the home tile (1–4 events). Shared pattern via `domain/home_tile_size.h`; set `supports_home_tile_size` on the manifest. Chips must exist on **portal Agenda** and **Agent → No Mac**. |

Event list on the home tile is cached in RAM from the last `CALENDAR` USB line. An empty catalog while the agent is offline does **not** clear link/events.

## Icons

Material Symbols (`calendar_month`, `event_busy`, `warning`) — Apache 2.0.  
Regenerate: `python3 tools/gen_calendar_icons.py`
