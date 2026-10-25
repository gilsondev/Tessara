#pragma once

#include <stddef.h>
#include <stdint.h>

// Material Symbols calendar (Apache-2.0), RGB565A8 white glyph + alpha.
namespace calendar_icons_data {

int side();
const uint8_t *month(size_t *bytes = nullptr);
const uint8_t *busy(size_t *bytes = nullptr);
const uint8_t *warn(size_t *bytes = nullptr);

}  // namespace calendar_icons_data
