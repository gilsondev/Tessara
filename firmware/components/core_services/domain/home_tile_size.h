#pragma once

#include <stddef.h>
#include <stdint.h>

/** Shared home-grid tile height for widgets that opt in (calendar, future tiles). */
namespace home_tile {

enum class Size : uint8_t {
  S = 0,
  M = 1,
  L = 2,
  XL = 3,
};

/** Widget settings key (namespaced per widget id). */
inline constexpr const char *kSettingKey = "home_tile_size";

Size parse(const char *value);
const char *to_string(Size size);

/** Content rows for list-style tiles (e.g. calendar events). */
int content_rows(Size size);

/** Minimum body height in px (below title row). */
int body_min_height_px(Size size);

/** Total tile min height = chrome (~40) + body. */
int tile_min_height_px(Size size);

Size clamp(int ordinal);

}  // namespace home_tile
