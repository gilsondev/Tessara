# Touch fluidity Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Press feedback + list scroll physics closer to phone feel; measure on sim + JC3248.

**Architecture:** Centralize in `kit::press_feedback` / `kit::enable_list_scroll`; apply from buttons/cards and calendar/weather lists. Media/focus realtime already lands in agentd — verify under load while measuring UI PERF.

## Tasks

1. kit press_feedback (scale ~0.96 + opa, 100ms ease_out, larger ext_click)
2. kit enable_list_scroll (momentum + elastic); wire calendar/weather
3. Native cover snapshot tests
4. make finish-host; flash; PERF + DRAG on device
