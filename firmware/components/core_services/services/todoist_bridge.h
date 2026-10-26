#pragma once

#include "domain/model.h"

#include <stddef.h>
#include <stdint.h>

// USB snapshot from agentd (`TODOIST` / `TODOIST_TASKS` / `TODOIST_END`).
// Heavy payload lives in PSRAM (or heap on native); the controller must not
// embed Snapshot as a member (blows internal DRAM).
namespace todoist_bridge {

struct ViewPayload {
    ViewItem item;
    TaskItem tasks[kMaxTasks];
    int count = 0;
    // True only after a TODOIST_TASKS line for this index — missing lines must
    // not wipe a previous Ready cache (USB bundles can overlap on the wire).
    bool tasks_received = false;
};

struct Snapshot {
    bool bridge_ok = false;
    bool linked = false;
    bool ready = false;
    bool stale = false;
    uint32_t received_ms = 0;
    ViewPayload views[kMaxViews];
    int view_count = 0;
};

struct Status {
    bool bridge_ok = false;
    bool linked = false;
    bool ready = false;
    bool stale = false;
    uint32_t received_ms = 0;
};

// Returns true only on a complete snapshot (END, or unlinked header).
// On success, `last()` points at the PSRAM/heap buffer (valid until next apply).
bool apply_line(const char *line, uint32_t now_ms);
const Snapshot *last();
Status status();

bool apply_close_ack(const char *line, bool *ok, char *id, size_t id_cap);
bool refresh_age(uint32_t now_ms);

constexpr uint32_t kStaleMs = 180000;

}  // namespace todoist_bridge
