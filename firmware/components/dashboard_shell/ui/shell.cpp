#include "ui/ui.h"

#include "platform/host_services_bind.h"
#include "tessara/register_builtin.h"
#include "tessara/runtime.h"
#include "ui/aitokens_page.h"
#include "ui/clock_page.h"
#include "ui/detail_host.h"
#include "ui/home.h"
#include "ui/loader.h"
#include "ui/settings_panel.h"
#include "ui/setup_panel.h"
#include "ui/todoist_panel.h"
#include "ui/weather_icons.h"
#include "ui/weather_page.h"
#include "aitokens_widget.h"
#include "calendar_widget.h"
#include "clock_widget.h"
#include "mac_widget.h"
#include "todoist_widget.h"
#include "weather_widget.h"
#include "ui/calendar_page.h"
#include "ui/mac_page.h"

#include <cstring>
#include <lvgl.h>

namespace {

UiEvents *events = nullptr;

void on_back(lv_event_t *) {
    if (events) events->on_back_home();
}

void on_refresh(lv_event_t *) {
    if (events) events->on_refresh_requested();
}

void settings_changed(const DeviceSettings &value) {
    if (events) events->on_settings_changed(value);
}

void settings_dismissed() {
    if (events) events->on_settings_closed();
}

void on_settings(lv_event_t *) {
    if (events) events->on_open_settings();
    settings_panel::open(
        settings_changed, settings_dismissed,
        []() {
            if (events) events->on_aitokens_refresh();
        },
        [](const char *widget_id, const char *key, const char *value) {
            if (events) events->on_widget_pref(widget_id, key, value);
        });
}

void on_open_widget(lv_event_t *e) {
    const char *id = static_cast<const char *>(lv_event_get_user_data(e));
    if (events && id) events->on_open_widget(id);
}

void on_aitokens_refresh(lv_event_t *) {
    if (events) events->on_aitokens_refresh();
}

void on_mac_cmd(const char *cmd) {
    if (events && cmd) events->on_mac_cmd(cmd);
}

bool todoist_idle_allowed() {
    return !loader::visible() && !settings_panel::visible() && !setup_panel::visible();
}

void show_todoist_in_slot(void *ui_parent) {
    auto *parent = static_cast<lv_obj_t *>(ui_parent);
    if (parent) todoist_panel::attach(parent);
    else todoist_panel::show();
}

}  // namespace

void ui::begin(UiEvents &handler) {
    events = &handler;
    todoist_panel::Hooks hooks;
    hooks.on_back = on_back;
    hooks.on_refresh = on_refresh;
    hooks.on_view_shown = [](int view) {
        if (events) events->on_view_shown(view);
    };
    hooks.on_task_checked = []() {
        if (events) events->on_task_checked();
    };
    hooks.on_task_completed = [](int view, int row) {
        if (events) events->on_task_completed(view, row);
    };
    hooks.idle_allowed = todoist_idle_allowed;
    todoist_panel::begin(hooks);

    weather_page::begin(on_back);
    aitokens_page::begin(on_back, on_aitokens_refresh);
    calendar_page::begin(on_back);
    mac_page::begin(on_back, on_mac_cmd);
    clock_page::begin(on_back);
    detail_host::begin();
    tessara::bind_host_services();
    tessara::register_builtin_widgets();
    tessara::todoist_widget_bind_show(show_todoist_in_slot);
    home::begin(on_open_widget, on_settings);
    home::show();
}

void ui::set_views(int selected) { todoist_panel::set_views(selected); }

void ui::view_changed(int view) { todoist_panel::view_changed(view); }

void ui::drop_row(int view, int row) { todoist_panel::drop_row(view, row); }

void ui::set_status(const char *text) { todoist_panel::set_status(text); }

void ui::show_home() {
    // Detach long-lived roots before cleaning the slot (cannot parent==nullptr).
    todoist_panel::attach(nullptr);
    aitokens_page::attach(nullptr);
    tessara::runtime().unmount_all();
    detail_host::close();
    home::show();
}

void ui::rebuild_home(const dashboard::Layout &layout) { home::rebuild(layout); }

void ui::show_widget(const char *widget_id) {
    if (!widget_id || !widget_id[0]) return;
    // Park long-lived panels before slot clean / remount.
    todoist_panel::attach(nullptr);
    aitokens_page::attach(nullptr);
    lv_obj_t *slot = detail_host::open();
    tessara::runtime().mount_exclusive(widget_id, slot);
}

void ui::show_todoist() { show_widget(tessara::todoist_widget_id()); }

void ui::show_weather() { show_widget(tessara::weather_widget_id()); }

void ui::show_aitokens() { show_widget(tessara::aitokens_widget_id()); }

void ui::show_clock() { show_widget(tessara::clock_widget_id()); }

void ui::aitokens_refreshing() { aitokens_page::show_refreshing(); }

void ui::aitokens_refresh_done() { aitokens_page::refresh_done(); }

void ui::set_summary(int total, bool pending) { home::set_summary(total, pending); }

void ui::set_weather(const weather::View &view) {
    home::set_weather(view);
    int slot = tessara::runtime().find_type(tessara::weather_widget_id());
    if (slot >= 0) tessara::weather_widget_paint(tessara::runtime().widget(slot), view);
    else if (!detail_host::visible()) {
        // Detail not open — cache-only home update.
    } else {
        weather_page::paint(view);
    }
}

void ui::set_aitokens(const aitokens::View &view) {
    home::set_aitokens(view);
    int slot = tessara::runtime().find_type(tessara::aitokens_widget_id());
    if (slot >= 0) tessara::aitokens_widget_paint(tessara::runtime().widget(slot), view);
    else aitokens_page::paint(view);
    settings_panel::show_aitokens(view);
}

void ui::set_calendar(const calendar::View &view) {
    home::set_calendar(view);
    int slot = tessara::runtime().find_type(tessara::calendar_widget_id());
    if (slot >= 0) tessara::calendar_widget_paint(tessara::runtime().widget(slot), view);
    else calendar_page::paint(view);
}

void ui::set_mac(const mac::View &view, bool agent_ok) {
    home::set_mac(view, agent_ok);
    int slot = tessara::runtime().find_type(tessara::mac_widget_id());
    if (slot >= 0) tessara::mac_widget_paint(tessara::runtime().widget(slot), view, agent_ok);
    else mac_page::paint(view, agent_ok);
}

void ui::set_agent(const agent::View &view, bool pack_on_sd, const char *pack_version) {
    settings_panel::show_agent(view, pack_on_sd, pack_version);
}

void ui::apply_theme() {
    weather_icons::apply_theme();
    home::apply_theme();
    weather_page::apply_theme();
    calendar_page::apply_theme();
    mac_page::apply_theme();
    aitokens_page::apply_theme();
    clock_page::apply_theme();
    todoist_panel::apply_theme();
}

void ui::set_device(const DeviceSettings &settings, const DeviceInfo &info) {
    settings_panel::show_values(settings, info);
    home::set_link(info.online, info.rssi);
}

void ui::set_face(const char *timezone, const char *time_format, const char *date_format) {
    settings_panel::show_face(timezone, time_format, date_format);
}

void ui::show_loader(const char *title, const char *detail) { loader::show(title, detail); }

void ui::set_loader_progress(int percent) { loader::set_progress(percent); }

void ui::hide_loader() { loader::hide(); }

void ui::show_setup(const char *title, const char *headline, const char *name, const char *hint, const char *url) {
    setup_panel::show(title, headline, name, hint, url);
}

void ui::hide_setup() { setup_panel::hide(); }

void ui::show_token_needed(const char *url) {
    show_todoist();
    todoist_panel::set_body_hidden(true);
    setup_panel::show_token(url);
    todoist_panel::set_status("falta o token");
}

void ui::hide_token_needed() {
    if (!setup_panel::token_visible()) return;
    setup_panel::hide_token();
    todoist_panel::set_body_hidden(false);
}

bool ui::loader_visible() { return loader::visible(); }

void ui::relayout() {
    home::arrange();
    loader::relayout();
    settings_panel::relayout();
    bool token = setup_panel::token_visible();
    setup_panel::relayout();
    if (token) {
        todoist_panel::set_body_hidden(true);
        return;
    }
    todoist_panel::relayout();
}

int ui::active_view() { return todoist_panel::active_view(); }

bool ui::idle() { return todoist_panel::idle(); }

ui::Stats ui::take_stats() {
    todoist_panel::Stats panel = todoist_panel::take_stats();
    Stats out = {};
    out.strip_us = panel.strip_us;
    out.land_us = panel.land_us;
    out.preview_us = panel.preview_us;
    out.repaint_us = panel.repaint_us;
    out.sliding = panel.sliding;
    out.tracking = panel.tracking;
    out.completing = panel.completing;
    return out;
}
