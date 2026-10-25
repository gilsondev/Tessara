#include "ui/calendar_icons.h"

#include "ui/calendar_icons_data.h"

namespace {

struct Icon {
    lv_image_dsc_t dsc{};
    bool ready = false;
};

Icon month_icon;
Icon busy_icon;
Icon warn_icon;

void ensure(Icon &icon, const uint8_t *(*fetch)(size_t *)) {
    if (icon.ready) return;
    size_t bytes = 0;
    const uint8_t *data = fetch(&bytes);
    const int side = calendar_icons_data::side();
    icon.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    icon.dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
    icon.dsc.header.flags = 0;
    icon.dsc.header.w = side;
    icon.dsc.header.h = side;
    icon.dsc.header.stride = side * 2;
    icon.dsc.data_size = bytes;
    icon.dsc.data = data;
    icon.ready = true;
}

}  // namespace

const lv_image_dsc_t *calendar_icons::month() {
    ensure(month_icon, calendar_icons_data::month);
    return &month_icon.dsc;
}

const lv_image_dsc_t *calendar_icons::busy() {
    ensure(busy_icon, calendar_icons_data::busy);
    return &busy_icon.dsc;
}

const lv_image_dsc_t *calendar_icons::warn() {
    ensure(warn_icon, calendar_icons_data::warn);
    return &warn_icon.dsc;
}
