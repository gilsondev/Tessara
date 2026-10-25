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
const calendar::View &last_view();

}  // namespace calendar_page
