#pragma once

#include "calendar/snapshot.h"

#include <lvgl.h>

namespace calendar_page {

void begin(lv_event_cb_t on_back);
void mount(lv_obj_t *parent);
void unmount();
void show();
void apply_theme();
void paint(const calendar::View &view);
// Device weekday (0 = Sunday) so day cards past tomorrow can be named; -1 when the clock is unknown.
void set_weekday(int wday);
const calendar::View &last_view();

}  // namespace calendar_page
