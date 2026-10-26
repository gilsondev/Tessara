# Media + Focus realtime Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Push MEDIA/FOCUS on macOS distributed notifications; real Focus list only; ship MEDIA_COVER RGB565 to the Mac page.

**Architecture:** `agentd` background `mac_events` thread posts `HostWake` on DNC/FSEvents; `usb_loop` drains wakes and pushes immediately. `caps-media` fetches artwork and emits chunked `MEDIA_COVER`. Firmware reassembles into an `lv_image`.

**Tech notes:** CFNotificationCenter distributed (no MediaRemote). Cover via Spotify `artwork url` / Music artwork export + `image` crate → RGB565. Focus drops CONTROL_CENTER_FALLBACK.

## Tasks

### Task 1: HostWake bus + DNC thread
- Create `agentd/src/mac_events.rs`
- Observe Music/Spotify/DND names; channel to USB loop
- Tests: unit for wake enum / non-mac stub

### Task 2: usb_loop drains wakes
- On Media/Focus wake → build_line + write if changed
- Raise MEDIA/FOCUS fallback poll to 8 s

### Task 3: Focus real modes only
- `list_modes` returns DB only (empty if missing)
- Golden/tests update

### Task 4: MEDIA_COVER encode + fetch
- caps-media: cover fetch + hex chunks
- handle MEDIA_COVER? / push after media change

### Task 5: Firmware cover buffer + Mac page image
- Parse MEDIA_COVER chunks; show 96×96 on mac_page

### Task 6: finish-host
- `make test` (+ agentd cargo test); note e2e if touch UI unchanged
