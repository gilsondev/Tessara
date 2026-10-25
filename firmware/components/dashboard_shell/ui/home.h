#pragma once

#include "aitokens/snapshot.h"
#include "calendar/snapshot.h"
#include "domain/dashboard_layout.h"
#include "weather/cache.h"

#include <lvgl.h>

// The dashboard. Clock, link and the Todoist card update in place.
namespace home {

// on_open_widget receives LV_EVENT_CLICKED with user_data = widget id (stable C string).
void begin(lv_event_cb_t on_open_widget, lv_event_cb_t on_settings);
// Tear down and remount home tiles from `layout` (portal toggles — no reboot).
void rebuild(const dashboard::Layout &layout);
void show();
void arrange();
void set_clock(const char *time, const char *date, const char *hello = nullptr);
void set_link(bool online, int rssi = 0);
void set_battery(int percent);
void set_summary(int total, bool pending);
void set_weather(const weather::View &view);
void set_aitokens(const aitokens::View &view);
void set_calendar(const calendar::View &view);
void apply_theme();

}  // namespace home
