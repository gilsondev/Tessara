# Screen rest (flip NeXtime) Implementation Plan

> **For agentic workers:** Implement task-by-task. Steps use checkbox syntax.

**Goal:** After 5 min idle, show a floating NeXtime-style flip clock for 1 min, fade brightness ~10 s, then backlight off; first touch only wakes.

**Architecture:** `screen_rest` owns phases/timers; `screensaver` UI is a fullscreen overlay with flip digits + drift; `input_host` swallows the wake gesture; `display_host::set_brightness` drives fade/off. Console `REST` forces phases for SNAP tests.

**Tech Stack:** LVGL 9, existing `display_host` / `input_host` / controller loop, native Unity tests for phase machine.

## Global Constraints

- Idle → saver: 5 min; saver visible: 1 min; fade: ~10 s; then off.
- First touch after saver/off wakes only (no UI click).
- No Settings UI in MVP.
- FLASH/loader must not enter rest.
- Network/Bridge keep running while off.

---

### Task 1: Phase machine + tests

**Files:**
- Create: `src/platform/screen_rest.h`, `src/platform/screen_rest.cpp`
- Create/extend: `test/test_native/test_screen_rest.cpp`, `test_smoke.cpp`

- [x] Failing tests: Awake→Saver at 5 min; Saver→Fading at +1 min; Fading→Off after fade; `note_activity` resets to Awake; wake flag until consumed.
- [x] Implement pure phase logic (no LVGL).
- [x] Native tests pass.

### Task 2: Screensaver overlay (flip clock)

**Files:**
- Create: `src/ui/screensaver.h`, `src/ui/screensaver.cpp`
- Wire show/hide from controller

- [x] Fullscreen black overlay on its own LVGL screen or top layer.
- [x] Four flip digits HH:MM; flip anim on minute change.
- [x] Drift position every ~15–20 s.
- [x] `show` / `hide` / `tick(now)`.

### Task 3: Integrate brightness fade + input wake

**Files:**
- Modify: `src/platform/input_host.*`, `src/platform/display_host.*` (if needed), `src/app/controller.cpp`, `src/platform/console.*`

- [x] Controller loop: `screen_rest::tick`; enter saver → `screensaver::show`; fading → interpolate brightness; off → brightness 0.
- [x] On wake: restore brightness, hide saver, `input_host` ignore until finger up.
- [x] Skip rest while FLASH loader visible.
- [x] Console: `REST`, `REST saver`, `REST fade`, `REST off`, `REST wake`.

### Task 4: Flash + SNAP transitions

- [x] Flash firmware.
- [x] Script: home → `REST saver` → SNAP; wait/force flip → SNAP; `REST fade` mid → SNAP; `REST off` → SNAP; wake drag → SNAP.
- [x] Confirm first drag after off does not open a card.
