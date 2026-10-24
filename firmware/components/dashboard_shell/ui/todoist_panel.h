#pragma once

#include <lvgl.h>
#include <stdint.h>

// Todoist detail: chips + list + horizontal slide between views.
// Shared by device shell and desktop simulator (same source).
namespace todoist_panel {

using BackFn = void (*)(lv_event_t *);
using RefreshFn = void (*)(lv_event_t *);
using ViewShownFn = void (*)(int view);
using TaskCheckedFn = void (*)();
using TaskCompletedFn = void (*)(int view, int row);
using IdleAllowedFn = bool (*)();

struct Hooks {
    BackFn on_back = nullptr;
    RefreshFn on_refresh = nullptr;
    ViewShownFn on_view_shown = nullptr;
    TaskCheckedFn on_task_checked = nullptr;
    TaskCompletedFn on_task_completed = nullptr;
    // When null, idle housekeeping always runs. Shell gates on overlay panels.
    IdleAllowedFn idle_allowed = nullptr;
};

// Worst-case cost of heavy UI steps since the last call, in microseconds.
struct Stats {
    uint32_t strip_us;
    uint32_t land_us;
    uint32_t preview_us;
    uint32_t repaint_us;
    bool sliding;
    bool tracking;
    int completing;
};

void begin(const Hooks &hooks);
void show();
// Mount into detail_host slot (parent). parent=nullptr parks under layer_top hidden.
// Root is never an LVGL screen — screens cannot be reparented.
void attach(lv_obj_t *parent);
lv_obj_t *screen();
// Rebuilds chips from task_store and shows `active`.
void set_views(int active);
void view_changed(int view);
void drop_row(int view, int row);
void set_status(const char *text);
// Hides chips + list (token overlay). Unhide refreshes status from task_store.
void set_body_hidden(bool hidden);
void apply_theme();
void relayout();
int active_view();
bool idle();
Stats take_stats();

}  // namespace todoist_panel
