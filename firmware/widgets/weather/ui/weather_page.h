#pragma once

#include "weather/cache.h"

#include <lvgl.h>

namespace weather_page {

// Saves the back handler (UI is created on mount into the shell slot).
void begin(lv_event_cb_t on_back);
// Mount detail UI as a child of `parent` (detail_host slot). Replaces any prior tree.
void mount(lv_obj_t *parent);
void unmount();
// Legacy: if nothing mounted, builds a standalone screen (sim without detail_host).
void show();
void apply_theme();
void paint(const weather::View &view);

}  // namespace weather_page
