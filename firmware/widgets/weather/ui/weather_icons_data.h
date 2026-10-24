#pragma once

#include <stddef.h>
#include <stdint.h>

// Meteocons fill (MIT), RGB565A8 (color plane then alpha) for LVGL.
namespace weather_icons_data {

int side();
const uint8_t *clear_day(size_t *bytes = nullptr);
const uint8_t *partly_cloudy(size_t *bytes = nullptr);
const uint8_t *overcast(size_t *bytes = nullptr);
const uint8_t *fog(size_t *bytes = nullptr);
const uint8_t *rain(size_t *bytes = nullptr);
const uint8_t *snow(size_t *bytes = nullptr);
const uint8_t *thunderstorms(size_t *bytes = nullptr);
const uint8_t *unavailable(size_t *bytes = nullptr);

}  // namespace weather_icons_data
