#pragma once

#include <lvgl.h>

// Bitmaps for WMO condition groups (and a generic empty state). Transparent background (RGB565A8).
namespace weather_icons {

const lv_image_dsc_t *for_code(int code);
const lv_image_dsc_t *unavailable();
void apply_theme();

}  // namespace weather_icons
