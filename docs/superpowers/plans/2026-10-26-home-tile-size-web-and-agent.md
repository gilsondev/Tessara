# Home tile size — web + Agente Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user set each opted-in widget’s home tile height (`home_tile_size` S–XL) from the **web portal** and from the **Agent app**, both writing the same device-namespaced setting.

**Architecture:** Device NVS remains source of truth via `/api/widget_settings`. Portal already has Agenda chips for calendar. Agent-ui gains a panel proxy for widget settings (IPC → `tessera.local`) and UI chips on widget pages. Widgets with `supports_home_tile_size` must expose chips on **both** surfaces.

**Tech Stack:** firmware web_page / web_portal, agentd panel IPC, apps/agent-ui, `domain/home_tile_size.h`.

**Spec:** `docs/superpowers/specs/2026-10-26-home-tile-size-calendar-cache-design.md` (§ “Onde o usuário escolhe a altura”).

## Global Constraints

- Key: `home_tile_size` values `S`|`M`|`L`|`XL` default `M` (see `home_tile::kSettingKey`).
- Source of truth: device widget settings — never a Mac-only copy.
- Agent offline / panel unreachable: show disabled chips + “painel offline”; do not write local fake height.
- First consumer: `org.tessera.calendar`. Later: aitokens/mac when they set `supports_home_tile_size`.
- Finish gate: `make test`, `make e2e` before flash.

## File map

| File | Role |
|---|---|
| `firmware/.../web_page.h` / settings.html source | Portal chips (calendar done; pattern for more widgets) |
| `firmware/.../web_portal.cpp` | Snapshot includes `widget_settings.*.home_tile_size` |
| `agentd/.../ipc.rs` (panel) | `POST /v1/panel/widget_settings` → device `/api/widget_settings` |
| `apps/agent-ui/IPC.md` + `src/ipc.js` | Client helper |
| `apps/agent-ui` UI | Agenda / Widgets section: Altura do tile chips |
| `docs/prototypes/agent-ui/README.md` | Note that widget height lives here too |

---

### Task 1: Confirm / complete portal coverage

**Files:**
- Modify as needed: portal Agenda / future widget pages
- Verify: `web_portal` snapshot exports `widget_settings["org.tessera.calendar"].home_tile_size`

- [ ] **Step 1:** Open portal Agenda — chips S/M/L/XL call `/api/widget_settings` with `id=org.tessera.calendar`, `key=home_tile_size`.

- [ ] **Step 2:** If snapshot omits the key after reboot, ensure publish path fills default `"M"` (controller already copies `calendar_home_tile_size` — keep in sync).

- [ ] **Step 3:** Document in portal note: “Mesma opção existe no Agente Tessera quando o painel está online.”

- [ ] **Step 4: Commit** only if copy/HTML changed.

---

### Task 2: agentd — proxy widget_settings

**Files:**
- Modify: agentd IPC panel routes
- Modify: `apps/agent-ui/IPC.md`
- Test: unit or smoke that POST body is forwarded

**Interfaces:**
- Consumes: `{ id, key, value }` (same as portal)
- Produces: device NVS update + refreshed panel state including `widget_settings`

- [ ] **Step 1: Add route**

```
POST /v1/panel/widget_settings
body: { "id": "org.tessera.calendar", "key": "home_tile_size", "value": "L" }
→ HTTP POST http://tessera.local/api/widget_settings (or current panel base URL)
```

Reuse the same host discovery as `/v1/panel/settings`.

- [ ] **Step 2: Ensure GET /v1/panel/state** includes `widget_settings` from portal JSON (already if full snapshot; if stripped, include it).

- [ ] **Step 3: Update IPC.md** with the new row.

- [ ] **Step 4: cargo test / manual curl + commit**

```bash
git add agentd apps/agent-ui/IPC.md
git commit -m "$(cat <<'EOF'
feat(agentd): proxy widget_settings so Agent can set home_tile_size

EOF
)"
```

---

### Task 3: agent-ui — chips “Altura do tile”

**Files:**
- Modify: agent-ui screens (Painel / Agenda / Widgets — match prototype language)
- Modify: `src/ipc.js` — `postWidgetSettings(id, key, value)`

- [ ] **Step 1: UI block** for Calendar (and later any `supports_home_tile_size` widget):

```text
Altura do tile
[ S ] [ M ] [ L ] [ XL ]
```

Read current value from `panel.state.widget_settings[id].home_tile_size` (default `M`).

- [ ] **Step 2: On click** → `POST /v1/panel/widget_settings`; toast “Salvo” / error if panel offline.

- [ ] **Step 3: Disable chips** when `usb` disconnected and Wi‑Fi panel unreachable; show “Ligue o cabo ou a rede do painel”.

- [ ] **Step 4: Commit**

```bash
git add apps/agent-ui
git commit -m "$(cat <<'EOF'
feat(agent-ui): home tile height chips synced with device settings

EOF
)"
```

---

### Task 4: Pattern for next widgets (AI Tokens, Mac)

**Files:**
- Spec already allows opt-in; when implementing AI Tokens height, add portal + agent chips in the **same** PR.

- [ ] **Step 1:** Checklist in widget README: portal chips + agent chips + `supports_home_tile_size=true`.

- [ ] **Step 2:** No commit required until a second widget opts in.

---

### Task 5: Finish gate

- [ ] `make test`
- [ ] `make e2e`
- [ ] Manual: set L on portal → home calendar taller; set S on Agent → same setting round-trips; offline Agent does not invent height
- [ ] `make flash` when board ready

---

## Dependency

Can run in parallel with `2026-10-26-aitokens-providers-stability` Task 5 (both extend agent-ui widget config). Prefer landing **Task 2 (IPC proxy)** first so both AI Tokens toggles and tile height share the panel bridge.
