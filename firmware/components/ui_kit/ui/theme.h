#pragma once

#include "fonts.h"
#include "ui/palette.h"

#include <lvgl.h>

namespace theme {

inline lv_color_t background() { return lv_color_hex(palette::background()); }
inline lv_color_t surface() { return lv_color_hex(palette::surface()); }
inline lv_color_t surface_elevated() { return lv_color_hex(palette::surface_elevated()); }
inline lv_color_t text_primary() { return lv_color_hex(palette::text_primary()); }
inline lv_color_t text_secondary() { return lv_color_hex(palette::text_secondary()); }
inline lv_color_t border() { return lv_color_hex(palette::border()); }
inline lv_color_t primary() { return lv_color_hex(palette::primary()); }
inline lv_color_t on_primary() { return lv_color_hex(palette::on_primary()); }
inline lv_color_t accent() { return lv_color_hex(palette::accent()); }
inline lv_color_t warning() { return lv_color_hex(palette::warning()); }
inline lv_color_t error() { return lv_color_hex(palette::error()); }
inline lv_color_t sky() { return lv_color_hex(palette::sky()); }
inline lv_color_t red() { return lv_color_hex(palette::red()); }
inline lv_color_t widget_todoist() { return lv_color_hex(palette::widget_todoist()); }
inline lv_color_t widget_calendar() { return lv_color_hex(palette::widget_calendar()); }
inline lv_color_t widget_weather() { return lv_color_hex(palette::widget_weather()); }
inline lv_color_t widget_aitokens() { return lv_color_hex(palette::widget_aitokens()); }

inline lv_color_t paper() { return background(); }
inline lv_color_t ink() { return text_primary(); }
inline lv_color_t muted() { return text_secondary(); }
inline lv_color_t wash() { return surface(); }
inline lv_color_t hairline() { return border(); }
inline lv_color_t sand() { return warning(); }

// Todoist API: 4 = P1, 3 = P2, 2 = P3. 1 is the natural level, with no priority set.
inline lv_color_t priority(uint8_t level) { return lv_color_hex(palette::priority(level)); }

#if defined(SIM_HOST_FONTS)
inline const lv_font_t &display_font() { return *sim_font_40; }
#else
inline const lv_font_t &display_font() { return lv_font_montserrat_40; }
#endif
inline const lv_font_t &title_font() { return font_pt18; }
inline const lv_font_t &body_font() { return font_pt16; }
inline const lv_font_t &label_font() { return font_pt15; }
inline const lv_font_t &small_font() { return font_pt13; }

constexpr int kRowPad = 7;
constexpr int kMarkSize = 22;
constexpr int kMarkReach = 13;  // extra touch margin around the check circle
constexpr int kListPad = 8;

}  // namespace theme
