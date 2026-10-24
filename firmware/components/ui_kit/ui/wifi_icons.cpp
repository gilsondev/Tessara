#include "ui/wifi_icons.h"

#include "ui/wifi_icons_data.h"

namespace {

struct Icon {
    lv_image_dsc_t dsc{};
    bool ready = false;
};

Icon strong_icon;
Icon medium_icon;
Icon weak_icon;
Icon off_icon;

void ensure(Icon &icon, const uint8_t *(*fetch)(size_t *)) {
    if (icon.ready) return;
    size_t bytes = 0;
    const uint8_t *data = fetch(&bytes);
    const int side = wifi_icons_data::side();
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

const lv_image_dsc_t *wifi_icons::for_strength(WifiStrength level) {
    switch (level) {
        case WifiStrength::Strong:
            ensure(strong_icon, wifi_icons_data::strong);
            return &strong_icon.dsc;
        case WifiStrength::Medium:
            ensure(medium_icon, wifi_icons_data::medium);
            return &medium_icon.dsc;
        case WifiStrength::Weak:
            ensure(weak_icon, wifi_icons_data::weak);
            return &weak_icon.dsc;
        default:
            ensure(off_icon, wifi_icons_data::off);
            return &off_icon.dsc;
    }
}
