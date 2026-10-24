#pragma once

#include "domain/device_settings.h"

#include <lvgl.h>

// Material Icons Wi-Fi por nível de sinal (Apache-2.0).
namespace wifi_icons {

const lv_image_dsc_t *for_strength(WifiStrength level);

}  // namespace wifi_icons
