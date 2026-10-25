#pragma once

#include "agent/state.h"
#include "aitokens/snapshot.h"
#include "calendar/snapshot.h"
#include "domain/dashboard_layout.h"
#include "mac/snapshot.h"
#include "domain/device_settings.h"
#include "weather/cache.h"

#include <stdint.h>

// What the screen asks of the application. Called from LVGL callbacks: keep handlers short, no network.
class UiEvents {
public:
    virtual ~UiEvents() = default;
    virtual void on_view_shown(int view) = 0;
    virtual void on_refresh_requested() = 0;
    virtual void on_settings_changed(const DeviceSettings &settings) = 0;
    // Namespaced widget pref from the on-device settings panel (clock/date face).
    virtual void on_widget_pref(const char *widget_id, const char *key, const char *value) = 0;
    // The check mark was tapped; the exit animation starts now.
    virtual void on_task_checked() = 0;
    // The row finished its exit animation; the task is still in task_store.
    virtual void on_task_completed(int view, int row) = 0;
    // Open a registered widget by stable id (org.tessara.*). Shell stays id-agnostic.
    virtual void on_open_widget(const char *widget_id) = 0;
    virtual void on_aitokens_refresh() = 0;
    // Mac media: "?" refresh, or "PLAYPAUSE" / "NEXT" / "PREV".
    virtual void on_mac_cmd(const char *cmd) = 0;
    virtual void on_back_home() = 0;
    virtual void on_open_settings() = 0;
    virtual void on_settings_closed() = 0;
};

// The task panel screen. Reads lists from task_store; everything runs on the LVGL thread.
namespace ui {

void begin(UiEvents &events);
void show_home();
void show_widget(const char *widget_id);
// Convenience wrappers (same as show_widget with fixed ids).
void show_todoist();
void show_weather();
void show_aitokens();
void show_clock();
// Remount home tiles from persisted/portal dashboard layout (no reboot).
void rebuild_home(const dashboard::Layout &layout);
void aitokens_refreshing();
void aitokens_refresh_done();
void set_summary(int total, bool pending);
void set_weather(const weather::View &view);
void set_aitokens(const aitokens::View &view);
void set_calendar(const calendar::View &view);
void set_mac(const mac::View &view, bool agent_ok);
void set_agent(const agent::View &view, bool pack_on_sd = false, const char *pack_version = nullptr);
void apply_theme();
// Rebuilds tabs from task_store's views and shows `active`.
void set_views(int active);
// task_store changed for `view`; repaints when it is on screen and the user is not touching it.
void view_changed(int view);
// Mirrors a row already removed from task_store.
void drop_row(int view, int row);
void set_status(const char *text);
// Values shown on the settings page.
void set_device(const DeviceSettings &settings, const DeviceInfo &info);
void set_face(const char *timezone, const char *time_format, const char *date_format);
void show_loader(const char *title, const char *detail);
void set_loader_progress(int percent);
void hide_loader();
bool loader_visible();
// First-run screen. `name` is the network to join, or the address to open.
void show_setup(const char *title, const char *headline, const char *name, const char *hint, const char *url);
void hide_setup();
// Keeps the top bar and replaces the task list with instructions for pasting the token.
void show_token_needed(const char *url);
void hide_token_needed();
void relayout();
int active_view();
bool idle();

// Worst-case cost of the heavy UI steps since the last call, in microseconds.
struct Stats {
    uint32_t strip_us;
    uint32_t land_us;
    uint32_t preview_us;
    uint32_t repaint_us;
    bool sliding;
    bool tracking;
    int completing;
};
Stats take_stats();

}  // namespace ui
