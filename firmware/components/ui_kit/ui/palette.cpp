#include "ui/palette.h"

namespace {
palette::Mode current = palette::Mode::Light;

uint32_t pick(uint32_t light, uint32_t dark) {
    return current == palette::Mode::Dark ? dark : light;
}
}  // namespace

void palette::set_mode(Mode mode) { current = mode; }

palette::Mode palette::mode() { return current; }

uint32_t palette::background() { return pick(0xF6F5F0, 0x101C1C); }
uint32_t palette::surface() { return pick(0xFFFFFF, 0x192B29); }
uint32_t palette::surface_elevated() { return pick(0xFFFFFF, 0x234039); }
uint32_t palette::text_primary() { return pick(0x172C2B, 0xEDF5EF); }
uint32_t palette::text_secondary() { return pick(0x526561, 0xA9BDB5); }
uint32_t palette::border() { return pick(0xD5DFDA, 0x36504A); }
uint32_t palette::primary() { return pick(0x147D73, 0x65D6C3); }
uint32_t palette::on_primary() { return pick(0xFFFFFF, 0x101C1C); }
uint32_t palette::accent() { return pick(0xBC632C, 0xF4B183); }
uint32_t palette::warning() { return accent(); }
uint32_t palette::error() { return 0xDC2626; }
uint32_t palette::sky() { return primary(); }
uint32_t palette::red() { return 0xE44332; }
uint32_t palette::widget_todoist() { return red(); }
uint32_t palette::widget_calendar() { return 0x1A73E8; }
uint32_t palette::widget_weather() { return 0x0284C7; }
uint32_t palette::widget_aitokens() { return 0x7C3AED; }
uint32_t palette::widget_mac() { return 0x5B8DEF; }

uint32_t palette::paper() { return background(); }
uint32_t palette::ink() { return text_primary(); }
uint32_t palette::muted() { return text_secondary(); }
uint32_t palette::wash() { return surface(); }
uint32_t palette::hairline() { return border(); }
uint32_t palette::sand() { return warning(); }

uint32_t palette::priority(uint8_t level) {
    switch (level) {
        case 4: return 0xD1453B;
        case 3: return 0xEB8909;
        case 2: return 0x246FE0;
        default: return 0x4A4A4A;
    }
}
