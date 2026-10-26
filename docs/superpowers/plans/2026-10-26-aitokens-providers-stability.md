# AI Tokens providers stability — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep AI Token providers visible when live fetch fails, show Cursor Modelos/Outros without summing on the home tile, persist per-provider monitor toggles from Agent scan, and expose AI Tokens widget config in agent-ui like the web portal.

**Architecture:** Agentd owns last-good cache per source + per-provider config.toml flags; firmware home/detail stop hiding enabled-but-unavailable rows and stop summing Cursor percentages; agent-ui scan/achados + AI Tokens settings talk to existing IPC (`/v1/scan`, panel settings).

**Tech Stack:** Rust (`caps-aitokens`, `scan`, `agentd`), C++ LVGL home/aitokens_page, agent-ui (Tauri/JS), native Unity + cargo tests.

**Spec:** `docs/superpowers/specs/2026-10-26-aitokens-providers-stability-design.md`

## Global Constraints

- Do not break `AITOKENS\tv1\t…` wire field order (firmware `aitokens::apply_line`).
- Cursor alerts use `100 − max(modelos, outros)`, never sum.
- Hide a provider row only when the user disabled it — never solely because `available=0`.
- Docs default English for new code comments; user-facing PT-BR copy stays PT.
- Finish gate: `make test` then `make e2e` before flash.

## File map

| File | Responsibility |
|---|---|
| `agentd/crates/caps-aitokens/src/lib.rs` | Last-good merge; respect per-source enable flags |
| `agentd/crates/caps-aitokens/src/cursor_live.rs` | Keep dual display; tolerate partial planUsage when possible |
| `agentd/crates/scan/src/lib.rs` | Detection (already); feed per-id enabled |
| `agentd/crates/agentd` config/IPC | `aitokens.cursor|claude|codex` in config.toml; apply on collect |
| `firmware/.../home.cpp` | Cursor line format; keep rows when unavailable |
| `firmware/widgets/aitokens/ui/aitokens_page.cpp` | Keep blocks visible with offline state |
| `apps/agent-ui` | Achados toggles persistence UX + AI Tokens settings page |
| `docs/superpowers/specs/2026-10-24-ai-tokens-design.md` | Patch “soma” line → dual % (pointer to new spec) |

---

### Task 1: Home tile — Cursor dual % (no sum) + keep unavailable rows

**Files:**
- Modify: `firmware/components/dashboard_shell/ui/home.cpp` (`home::set_aitokens`)
- Modify: `docs/superpowers/specs/2026-10-24-ai-tokens-design.md` (one-line supersede)
- Test: host visual via e2e after Task 3; unit via existing smoke if any string helper extracted

**Interfaces:**
- Consumes: `aitokens::Source.display` as `N% · M%` for cursor
- Produces: label text `Cursor · Modelos N% · Outros M%` (or compact `N% · M%` with unit `usados` without summing)

- [ ] **Step 1: Replace sum branch**

In `set_row` for `cursor`, delete `a + b`. Use:

```cpp
if (cursor) {
    int a = -1, b = -1;
    if (sscanf(src->display, "%d%% · %d%%", &a, &b) == 2 && a >= 0 && b >= 0) {
        snprintf(shown, sizeof(shown), "Modelos %d%% · Outros %d%%", a, b);
    } else {
        snprintf(shown, sizeof(shown), "%s", src->display);
    }
}
```

Adjust `unit` for cursor to `nullptr` or `"usados"` only if it still fits the label width; prefer embedding “usados” only in warn/alerta branches if needed.

- [ ] **Step 2: Do not hide unavailable enabled rows**

Change:

```cpp
const bool ok = view.ready && src && src->available && src->display[0];
if (!ok) {
    if (row) lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    return;
}
```

To: if `show` and (`!view.ready` → leave as empty/ligar Bridge at card level) else if `src` missing or `!available`:

```cpp
if (row) lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
set_if_changed(label, /* "Cursor · offline" / display if any */);
// skip worst-remaining aggregation for unavailable
return;
```

Only `if (!show) hide`.

- [ ] **Step 3: Patch old spec**

In `2026-10-24-ai-tokens-design.md`, replace “Cursor: soma dos % usados” with pointer to `2026-10-26-aitokens-providers-stability-design.md` §B.

- [ ] **Step 4: Commit**

```bash
git add firmware/components/dashboard_shell/ui/home.cpp docs/superpowers/specs/2026-10-24-ai-tokens-design.md
git commit -m "$(cat <<'EOF'
fix(aitokens): show Cursor dual % on home and keep offline rows

EOF
)"
```

---

### Task 2: Detail page — visible offline state for enabled sources

**Files:**
- Modify: `firmware/widgets/aitokens/ui/aitokens_page.cpp` (`paint_source`, `source_ok` usage)

**Interfaces:**
- Consumes: same `aitokens::View`
- Produces: block visible with “offline” / last display when `!available`

- [ ] **Step 1: Split “enabled but empty” from hide**

When `device.show_aitokens_*` is true and source is missing/unavailable, call `set_block_visible(block, true)` and set value/sub to a short PT status (`sem cotas`, `offline`, keep `display` if agent sent a reason string).

- [ ] **Step 2: Keep dual bars only when `available` and parse_dual_pct succeeds**

- [ ] **Step 3: Commit**

```bash
git add firmware/widgets/aitokens/ui/aitokens_page.cpp
git commit -m "$(cat <<'EOF'
fix(aitokens): keep detail blocks visible when a source is offline

EOF
)"
```

---

### Task 3: Agentd — last-good cache per source (keep numbers; expire after hours)

**Files:**
- Modify: `agentd/crates/caps-aitokens/src/lib.rs` (`AiTokensCapability`, merge helpers)
- Optional: `agentd/crates/caps-aitokens/src/last_good.rs` + persist under `~/.tessera/agentd/state/`
- Test: `#[cfg(test)]` in lib / last_good

**Interfaces:**
- Consumes: fresh `Vec<AiTokenSource>` from collectors
- Produces: merged list — on `available=false`, reuse last-good **values** (same display/value) until `AITOKENS_LAST_GOOD_TTL_S` (21600 = 6h) since last success; then drop cache and emit unavailable/empty numbers
- Do **not** leave the tile empty while cache is valid — user must still see prior %

- [ ] **Step 1: Write failing tests**

```rust
const LAST_GOOD_TTL_S: u64 = 6 * 60 * 60;

#[test]
fn preserves_last_good_values_when_next_poll_unavailable() {
    let good = AiTokenSource::available("cursor", SourceKind::Pct, 80, "10% · 20%", "01 jan");
    let bad = AiTokenSource::unavailable("cursor");
    let merged = merge_last_good(
        /* previous store */ &[LastGood { source: good.clone(), ok_at: Instant::now() }],
        /* fresh */ &[bad],
        Instant::now(),
        LAST_GOOD_TTL_S,
    );
    let c = merged.iter().find(|s| s.id == "cursor").unwrap();
    assert!(c.available, "must keep serving numbers, not empty");
    assert_eq!(c.display, "10% · 20%");
    assert_eq!(c.value, 80);
}

#[test]
fn clears_last_good_after_ttl_hours() {
    let good = AiTokenSource::available("cursor", SourceKind::Pct, 80, "10% · 20%", "01 jan");
    let bad = AiTokenSource::unavailable("cursor");
    let ok_at = Instant::now()
        .checked_sub(Duration::from_secs(LAST_GOOD_TTL_S + 60))
        .unwrap();
    let merged = merge_last_good(
        &[LastGood { source: good, ok_at }],
        &[bad.clone()],
        Instant::now(),
        LAST_GOOD_TTL_S,
    );
    let c = merged.iter().find(|s| s.id == "cursor").unwrap();
    assert!(!c.available);
    assert!(c.display.is_empty() || c.display != "10% · 20%");
}
```

- [ ] **Step 2: Run tests — expect FAIL**

```bash
cd agentd && cargo test -p tessera-caps-aitokens preserves_last_good -- --nocapture
cd agentd && cargo test -p tessera-caps-aitokens clears_last_good -- --nocapture
```

- [ ] **Step 3: Implement merge + store in `build_line`**

```text
USAGE_CACHE_TTL_S (~60s)     = how often to re-hit APIs (unchanged)
LAST_GOOD_TTL_S (21600 = 6h) = how long to keep last successful numbers when polls fail
```

On each collect, per id:
1. if `new.available` → save as last-good (`ok_at = now`), use `new`
2. else if last-good exists and `now - ok_at < LAST_GOOD_TTL_S` → use cloned last-good (still `available=true` on the wire so UI stays filled)
3. else → drop last-good; emit unavailable (UI shows offline row, not old %)

Prefer persist JSON to `~/.tessera/agentd/state/aitokens_last_good.json` (`id`, fields, `ok_at_unix`). If file I/O is heavy for this task, RAM-only is OK for first PR but keep the 6h TTL API identical.

- [ ] **Step 4: Soften cursor_live when only one of auto/api missing**

If one of `autoPercentUsed` / `apiPercentUsed` is present, treat missing as `0` instead of whole-source unavailable.

- [ ] **Step 5: cargo test package + commit**

```bash
cd agentd && cargo test -p tessera-caps-aitokens
git add agentd/crates/caps-aitokens
git commit -m "$(cat <<'EOF'
fix(agentd): cache last-good AI token values for 6h across failed polls

EOF
)"
```

---

### Task 4: Per-provider config.toml + collect filter

**Files:**
- Modify: agentd config types (`AgentdConfig`) + `write_config` / load
- Modify: `caps-aitokens` collect entry or agentd usb_loop to skip disabled ids
- Modify: `agentd/crates/agentd/src/ipc.rs` `config_from_scan` to set per-id flags
- Test: unit on config parse + scan→config

**Interfaces:**
- Consumes: scan `found[].id` + `enabled`
- Produces: e.g.

```toml
aitokens = true
aitokens_cursor = true
aitokens_claude = false
aitokens_codex = true
```

(or nested table if toml crate already supports — match existing style)

- [ ] **Step 1: Extend config + defaults (all true when `aitokens` true for back-compat)**

- [ ] **Step 2: `collect_sources` / encode only enabled ids (still fixed order if present)**

Empty slot policy: prefer **omit** disabled sources from the line (firmware already hides via `show_aitokens_*`). Document: device toggles vs agent collect toggles — agent controls push; device controls paint.

- [ ] **Step 3: Scan apply writes per-id flags without wiping unknown keys**

- [ ] **Step 4: Tests + commit**

```bash
cd agentd && cargo test -p tessera-scan && cargo test -p tessera-agentd
git add agentd/crates
git commit -m "$(cat <<'EOF'
feat(agentd): per-provider aitokens toggles from scan config

EOF
)"
```

---

### Task 5: agent-ui — AI Tokens settings + scan toggles wired

**Files:**
- Modify: `apps/agent-ui` screens (No Mac / Achados / new AI Tokens section)
- Modify: `apps/agent-ui/IPC.md` if new fields
- Reuse: `POST /v1/scan`, `GET /v1/scan/last`, `POST /v1/panel/settings` for `show_aitokens_*`
- Coordinate with `2026-10-26-home-tile-size-web-and-agent.md` for shared `/v1/panel/widget_settings` proxy + height chips on widget pages

**Interfaces:**
- Consumes: scan found states; panel snapshot `settings.show_aitokens_*`
- Produces: user can (1) enable collect per provider on Mac (2) enable home lines on device when panel reachable

- [ ] **Step 1: Achados UI persists `enabled` per cursor/claude/codex via existing scan apply**

- [ ] **Step 2: Add “AI Tokens” settings block** mirroring portal copy (Codex/Claude/Cursor na home) calling panel settings when USB/Wi‑Fi ok; when offline, only Mac collect toggles

- [ ] **Step 3: Onboarding copy: Mac novo = varrer; Mac já configurado = varrer de novo sem resetar toggles**

- [ ] **Step 4: Manual check + commit**

```bash
git add apps/agent-ui docs
git commit -m "$(cat <<'EOF'
feat(agent-ui): AI Tokens provider toggles aligned with portal

EOF
)"
```

---

### Task 6: Finish gate

- [ ] **Step 1:** `make test`

- [ ] **Step 2:** `make e2e` — inspect `$SIM_OUT` home/aitokens if scenarios cover it

- [ ] **Step 3:** `cd agentd && cargo test`

- [ ] **Step 4:** `make flash` when board available; verify: unplug Cursor network / force bad token briefly → tile still shows Cursor row; home shows Modelos/Outros not a sum

- [ ] **Step 5:** Final commit only if docs/code still dirty; otherwise stop

---

## Order / dependencies

```
Task 1 (home) ──┐
Task 2 (detail) ┼─► Task 6
Task 3 (cache) ─┤
Task 4 (config) ┼─► Task 5 (UI) ─► Task 6
```

Tasks 1–2 can ship without agentd for immediate UX fix on device once flashed with a good snapshot; Task 3–5 stop recurrence and complete the Agent story.
