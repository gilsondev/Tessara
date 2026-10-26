#include "services/todoist_bridge.h"

#include <cstdlib>
#include <cstring>

#if defined(ARDUINO) && defined(BOARD_HAS_PSRAM)
#include <esp_heap_caps.h>
#endif

namespace {

bool eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

bool next_field(char **cursor, char *out, size_t out_n) {
    if (!cursor || !*cursor || !**cursor) {
        if (out && out_n) out[0] = '\0';
        return false;
    }
    char *start = *cursor;
    char *end = start;
    while (*end && *end != '\t' && *end != '\r' && *end != '\n') ++end;
    size_t n = static_cast<size_t>(end - start);
    if (out && out_n) {
        if (n >= out_n) n = out_n - 1;
        memcpy(out, start, n);
        out[n] = '\0';
    }
    if (*end == '\t') *cursor = end + 1;
    else *cursor = end;
    return true;
}

todoist_bridge::Snapshot *slot = nullptr;
bool staging_open = false;
uint32_t staging_since = 0;
todoist_bridge::Status flags = {};
// A bundle is at most a few lines; anything older than this lost its END on the
// wire and must not block every later snapshot.
constexpr uint32_t kStagingAbandonMs = 4000;

// 4KB does not belong on the console task stack.
char parse_buf[4096];

todoist_bridge::Snapshot *ensure_slot() {
    if (slot) return slot;
#if defined(ARDUINO) && defined(BOARD_HAS_PSRAM)
    slot = static_cast<todoist_bridge::Snapshot *>(
        heap_caps_malloc(sizeof(todoist_bridge::Snapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    slot = static_cast<todoist_bridge::Snapshot *>(malloc(sizeof(todoist_bridge::Snapshot)));
#endif
    if (slot) memset(slot, 0, sizeof(*slot));
    return slot;
}

}  // namespace

bool todoist_bridge::apply_line(const char *line, uint32_t now_ms) {
    if (!line) return false;
    if (strncmp(line, "TODOIST_CLOSE", 13) == 0) return false;
    if (strncmp(line, "TODOIST_SEL", 11) == 0) return false;
    if (strncmp(line, "TODOIST_TOKEN", 13) == 0) return false;
    if (strchr(line, '?')) return false;

    Snapshot *snap = ensure_slot();
    if (!snap) return false;

    char *buf = parse_buf;
    strncpy(buf, line, sizeof(parse_buf) - 1);
    buf[sizeof(parse_buf) - 1] = '\0';
    char *cursor = buf;
    char field[64];

    if (staging_open && now_ms - staging_since > kStagingAbandonMs) staging_open = false;

    if (!strncmp(line, "TODOIST_TASKS\t", 14)) {
        if (!staging_open) return false;
        if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "TODOIST_TASKS")) return false;
        if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "v1")) return false;
        if (!next_field(&cursor, field, sizeof(field))) return false;
        int index = static_cast<int>(strtol(field, nullptr, 10));
        if (index < 0 || index >= snap->view_count) return false;
        if (!next_field(&cursor, field, sizeof(field))) return false;
        int n = static_cast<int>(strtol(field, nullptr, 10));
        if (n < 0) n = 0;
        if (n > kMaxTasks) n = kMaxTasks;
        ViewPayload &vp = snap->views[index];
        // First TASKS line for this view replaces; further chunks append (agent
        // splits long lists so each USB line stays under the console buffer).
        if (!vp.tasks_received) {
            vp.count = 0;
            vp.tasks_received = true;
        }
        for (int t = 0; t < n && vp.count < kMaxTasks; ++t) {
            TaskItem &task = vp.tasks[vp.count];
            if (!next_field(&cursor, task.id, sizeof(task.id))) break;
            if (!next_field(&cursor, field, sizeof(field))) break;
            int prio = static_cast<int>(strtol(field, nullptr, 10));
            if (prio < 1) prio = 1;
            if (prio > 4) prio = 4;
            task.priority = static_cast<uint8_t>(prio);
            if (!next_field(&cursor, task.content, sizeof(task.content))) break;
            if (!next_field(&cursor, task.meta, sizeof(task.meta))) task.meta[0] = '\0';
            if (task.id[0]) vp.count++;
        }
        return false;
    }

    if (!strncmp(line, "TODOIST_END\t", 12)) {
        if (!staging_open) return false;
        if (!next_field(&cursor, field, sizeof(field))) return false;
        if (!next_field(&cursor, field, sizeof(field))) return false;
        if (!next_field(&cursor, field, sizeof(field))) return false;
        snap->bridge_ok = field[0] == '1';
        if (!next_field(&cursor, field, sizeof(field))) return false;
        snap->linked = field[0] == '1';
        snap->ready = true;
        snap->stale = false;
        snap->received_ms = now_ms;
        flags.bridge_ok = snap->bridge_ok;
        flags.linked = snap->linked;
        flags.ready = snap->ready;
        flags.stale = false;
        flags.received_ms = now_ms;
        staging_open = false;
        return true;
    }

    if (strncmp(line, "TODOIST\t", 8) != 0) return false;
    // A second catalog mid-bundle used to memset staging and left later END
    // committing empty counts (e.g. Trabalho=0 while Mac cache still had tasks).
    // Stale staging was already dropped above, so this never wedges.
    if (staging_open) return false;
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "TODOIST")) return false;
    if (!next_field(&cursor, field, sizeof(field)) || !eq(field, "v1")) return false;
    if (!next_field(&cursor, field, sizeof(field))) return false;

    memset(snap, 0, sizeof(*snap));
    snap->bridge_ok = field[0] == '1';
    snap->received_ms = now_ms;
    if (!next_field(&cursor, field, sizeof(field))) return false;
    snap->linked = field[0] == '1';
    if (!next_field(&cursor, field, sizeof(field))) return false;
    int view_count = static_cast<int>(strtol(field, nullptr, 10));
    if (view_count < 0) view_count = 0;
    if (view_count > kMaxViews) view_count = kMaxViews;
    snap->view_count = 0;
    for (int v = 0; v < view_count; ++v) {
        ViewPayload &vp = snap->views[snap->view_count];
        if (!next_field(&cursor, vp.item.name, sizeof(vp.item.name))) break;
        if (!next_field(&cursor, vp.item.query, sizeof(vp.item.query))) break;
        vp.count = 0;
        vp.tasks_received = false;
        if (vp.item.name[0] && vp.item.query[0]) snap->view_count++;
    }
    staging_open = true;
    staging_since = now_ms;
    if (!snap->linked) {
        snap->ready = true;
        flags.bridge_ok = snap->bridge_ok;
        flags.linked = false;
        flags.ready = true;
        flags.stale = false;
        flags.received_ms = now_ms;
        staging_open = false;
        return true;
    }
    return false;
}

const todoist_bridge::Snapshot *todoist_bridge::last() { return slot; }

todoist_bridge::Status todoist_bridge::status() { return flags; }

bool todoist_bridge::apply_close_ack(const char *line, bool *ok, char *id, size_t id_cap) {
    if (!line || strncmp(line, "TODOIST_CLOSE\tv1\t", 17) != 0) return false;
    char buf[96];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *cursor = buf;
    char field[32];
    if (!next_field(&cursor, field, sizeof(field))) return false;
    if (!next_field(&cursor, field, sizeof(field))) return false;
    if (!next_field(&cursor, field, sizeof(field))) return false;
    if (ok) *ok = field[0] == '1';
    if (!next_field(&cursor, field, sizeof(field))) {
        if (id && id_cap) id[0] = '\0';
        return true;
    }
    if (id && id_cap) {
        strncpy(id, field, id_cap - 1);
        id[id_cap - 1] = '\0';
    }
    return true;
}

bool todoist_bridge::refresh_age(uint32_t now_ms) {
    if (!flags.ready) {
        bool changed = flags.stale;
        flags.stale = false;
        return changed;
    }
    bool was = flags.stale;
    flags.stale = now_ms - flags.received_ms > kStaleMs;
    return was != flags.stale;
}
