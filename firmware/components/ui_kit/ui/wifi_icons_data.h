#pragma once

#include <stddef.h>
#include <stdint.h>

// Material Icons Wi-Fi (Apache-2.0), RGB565A8 white glyph + alpha.
namespace wifi_icons_data {

int side();
const uint8_t *strong(size_t *bytes = nullptr);
const uint8_t *medium(size_t *bytes = nullptr);
const uint8_t *weak(size_t *bytes = nullptr);
const uint8_t *off(size_t *bytes = nullptr);

}  // namespace wifi_icons_data
