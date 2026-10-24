#pragma once

#include "tessara/services.h"

#include <lvgl.h>

namespace clock_page {

void begin(lv_event_cb_t on_back);
void mount(lv_obj_t *parent);
void unmount();
void show();
void apply_theme();
void paint(const tessara::TimeSnapshot &snap);

}  // namespace clock_page
