#include "domain/task_store.h"

#include "services/text.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <cstring>

namespace {
struct Slot {
    task_store::State state;
    int count;
    uint32_t version;
    uint32_t stamp;
    char error[48];
};

struct Park {
    bool used;
    char query[220];
    Slot slot;
};

ViewItem views[kMaxViews];
int views_n = 0;
Slot slots[kMaxViews];
Park parks[kMaxViews];
TaskItem *cache = nullptr;
TaskItem *mirror = nullptr;  // snapshot of the visible rows while they are rearranged
TaskItem *parked = nullptr;  // tasks for lists taken off the home
uint32_t next_version = 1;

bool valid(int view) { return view >= 0 && view < views_n; }
TaskItem *rows_at(TaskItem *base, int index) { return base + index * kMaxTasks; }
TaskItem *rows(int view) { return rows_at(cache, view); }

void copy_rows(TaskItem *dest, const TaskItem *src, int count) {
    if (count < 0) count = 0;
    if (count > kMaxTasks) count = kMaxTasks;
    if (count && dest && src) memcpy(dest, src, sizeof(TaskItem) * static_cast<size_t>(count));
}

int park_dest(const char *query) {
    for (int i = 0; i < kMaxViews; ++i) {
        if (parks[i].used && !strcmp(parks[i].query, query)) return i;
    }
    for (int i = 0; i < kMaxViews; ++i) {
        if (!parks[i].used) return i;
    }
    int oldest = 0;
    for (int i = 1; i < kMaxViews; ++i) {
        if (parks[i].slot.stamp < parks[oldest].slot.stamp) oldest = i;
    }
    return oldest;
}

void park_row(const char *query, const Slot &slot, const TaskItem *src) {
    if (!query || !query[0] || !parked || slot.state == task_store::State::Empty) return;
    int dest = park_dest(query);
    copy_rows(rows_at(parked, dest), src, slot.count);
    parks[dest].used = true;
    text::copy(parks[dest].query, sizeof(parks[dest].query), query);
    parks[dest].slot = slot;
}
}  // namespace

bool task_store::begin() {
    size_t bytes = sizeof(TaskItem) * kMaxTasks * kMaxViews;
    cache = static_cast<TaskItem *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    mirror = static_cast<TaskItem *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    parked = static_cast<TaskItem *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return cache && mirror && parked;
}

void task_store::set_views(const ViewItem *items, int count) {
    if (count > kMaxViews) count = kMaxViews;
    if (count < 0) count = 0;
    bool same = count == views_n;
    for (int i = 0; same && i < count; ++i) same = !strcmp(items[i].query, views[i].query);
    if (same) {
        for (int i = 0; i < count; ++i) views[i] = items[i];
        return;
    }
    if (!cache || !mirror || !parked) {
        for (int i = 0; i < count; ++i) views[i] = items[i];
        views_n = count;
        for (Slot &slot : slots) slot = Slot{State::Empty, 0, next_version++, 0, ""};
        return;
    }

    // PSRAM: these copies used to sit on the 16 KB loop stack during a USB line.
    static ViewItem *old_views = nullptr;
    static Slot *old_slots = nullptr;
    if (!old_views || !old_slots) {
        if (!old_views) {
            old_views = static_cast<ViewItem *>(
                heap_caps_malloc(sizeof(ViewItem) * kMaxViews, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        }
        if (!old_slots) {
            old_slots = static_cast<Slot *>(
                heap_caps_malloc(sizeof(Slot) * kMaxViews, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        }
    }
    if (!old_views || !old_slots) {
        for (int i = 0; i < count; ++i) views[i] = items[i];
        views_n = count;
        return;
    }
    int old_n = views_n;
    for (int i = 0; i < old_n; ++i) {
        old_views[i] = views[i];
        old_slots[i] = slots[i];
        copy_rows(rows_at(mirror, i), rows(i), old_slots[i].count);
    }

    int from_old[kMaxViews];
    int from_park[kMaxViews];
    bool old_used[kMaxViews] = {};
    for (int i = 0; i < count; ++i) {
        from_old[i] = -1;
        from_park[i] = -1;
        for (int j = 0; j < old_n; ++j) {
            if (old_used[j] || strcmp(old_views[j].query, items[i].query)) continue;
            from_old[i] = j;
            old_used[j] = true;
            break;
        }
        if (from_old[i] >= 0) continue;
        for (int p = 0; p < kMaxViews; ++p) {
            if (!parks[p].used || strcmp(parks[p].query, items[i].query)) continue;
            from_park[i] = p;
            break;
        }
    }

    for (int i = 0; i < count; ++i) {
        views[i] = items[i];
        if (from_old[i] >= 0) {
            slots[i] = old_slots[from_old[i]];
            copy_rows(rows(i), rows_at(mirror, from_old[i]), slots[i].count);
        } else if (from_park[i] >= 0) {
            slots[i] = parks[from_park[i]].slot;
            copy_rows(rows(i), rows_at(parked, from_park[i]), slots[i].count);
            parks[from_park[i]].used = false;
        } else {
            slots[i] = Slot{State::Empty, 0, next_version++, 0, ""};
        }
    }
    for (int j = 0; j < old_n; ++j) {
        if (!old_used[j]) park_row(old_views[j].query, old_slots[j], rows_at(mirror, j));
    }
    views_n = count;
    for (int i = count; i < kMaxViews; ++i) slots[i] = Slot{};
}

int task_store::view_count() { return views_n; }

const ViewItem &task_store::view(int index) { return views[valid(index) ? index : 0]; }

task_store::State task_store::state(int view) { return valid(view) ? slots[view].state : State::Empty; }

const TaskItem *task_store::tasks(int view, int *count) {
    *count = valid(view) && cache ? slots[view].count : 0;
    return valid(view) && cache ? rows(view) : nullptr;
}

uint32_t task_store::version(int view) { return valid(view) ? slots[view].version : 0; }

uint32_t task_store::age_ms(int view) {
    return valid(view) && slots[view].state != State::Empty ? millis() - slots[view].stamp : UINT32_MAX;
}

const char *task_store::error(int view) { return valid(view) ? slots[view].error : ""; }

void task_store::store(int view, const TaskItem *items, int count) {
    if (!valid(view) || !cache) return;
    if (count > kMaxTasks) count = kMaxTasks;
    Slot &slot = slots[view];
    bool same = slot.state == State::Ready && slot.count == count &&
                !memcmp(rows(view), items, sizeof(TaskItem) * static_cast<size_t>(count));
    if (!same) {
        memcpy(rows(view), items, sizeof(TaskItem) * static_cast<size_t>(count));
        slot.count = count;
        slot.version = next_version++;
    }
    slot.state = State::Ready;
    slot.stamp = millis();
    slot.error[0] = '\0';
}

void task_store::remember(const char *query, const TaskItem *items, int count) {
    if (!query || !query[0] || !items) return;
    for (int i = 0; i < views_n; ++i) {
        if (!strcmp(views[i].query, query)) {
            store(i, items, count);
            return;
        }
    }
    Slot slot{State::Ready, count > kMaxTasks ? kMaxTasks : count, next_version++, millis(), ""};
    park_row(query, slot, items);
}

void task_store::fail(int view, const char *message) {
    if (!valid(view)) return;
    Slot &slot = slots[view];
    slot.stamp = millis();
    text::copy(slot.error, sizeof(slot.error), message);
    if (slot.state == State::Ready) return;
    slot.state = State::Failed;
    slot.version = next_version++;
}

int task_store::remove(int view, const char *id) {
    if (!valid(view) || !cache) return -1;
    Slot &slot = slots[view];
    TaskItem *items = rows(view);
    for (int i = 0; i < slot.count; ++i) {
        if (strcmp(items[i].id, id)) continue;
        memmove(items + i, items + i + 1, sizeof(TaskItem) * static_cast<size_t>(slot.count - i - 1));
        slot.count--;
        slot.version = next_version++;
        return i;
    }
    return -1;
}

void task_store::invalidate_all() {
    for (int i = 0; i < views_n; ++i) slots[i].stamp = 0;
    for (Park &park : parks) {
        if (park.used) park.slot.stamp = 0;
    }
}
