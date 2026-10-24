#include "ui/aitokens_logos.h"

#include "ui/aitokens_logos_data.h"

namespace {

struct Logo {
    lv_image_dsc_t dsc{};
    bool ready = false;
};

Logo codex_logo;
Logo claude_logo;
Logo cursor_logo;

void ensure(Logo &logo, const uint8_t *(*fetch)(size_t *)) {
    if (logo.ready) return;
    size_t bytes = 0;
    const uint8_t *data = fetch(&bytes);
    const int side = aitokens_logos_data::side();
    logo.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    logo.dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
    logo.dsc.header.flags = 0;
    logo.dsc.header.w = side;
    logo.dsc.header.h = side;
    logo.dsc.header.stride = side * 2;
    logo.dsc.data_size = bytes;
    logo.dsc.data = data;
    logo.ready = true;
}

}  // namespace

const lv_image_dsc_t *aitokens_logos::codex() {
    ensure(codex_logo, aitokens_logos_data::codex);
    return &codex_logo.dsc;
}

const lv_image_dsc_t *aitokens_logos::claude() {
    ensure(claude_logo, aitokens_logos_data::claude);
    return &claude_logo.dsc;
}

const lv_image_dsc_t *aitokens_logos::cursor() {
    ensure(cursor_logo, aitokens_logos_data::cursor);
    return &cursor_logo.dsc;
}
