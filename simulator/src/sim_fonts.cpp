#include "sim_fonts.h"

#include <lvgl.h>
#include <libs/freetype/lv_freetype.h>

#include <cstdio>

const lv_font_t *sim_font_13 = &lv_font_montserrat_14;
const lv_font_t *sim_font_14 = &lv_font_montserrat_14;
const lv_font_t *sim_font_16 = &lv_font_montserrat_16;
const lv_font_t *sim_font_20 = &lv_font_montserrat_20;
const lv_font_t *sim_font_40 = &lv_font_montserrat_40;

namespace {

const char *kFontCandidates[] = {
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/Library/Fonts/Arial.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    nullptr,
};

lv_font_t *make(const char *path, uint32_t px) {
    return lv_freetype_font_create(path, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, px, LV_FREETYPE_FONT_STYLE_NORMAL);
}

}  // namespace

bool sim_fonts::init() {
    // lv_init() already calls lv_freetype_init when LV_USE_FREETYPE=1.
    const char *path = nullptr;
    for (int i = 0; kFontCandidates[i]; ++i) {
        FILE *f = std::fopen(kFontCandidates[i], "rb");
        if (!f) continue;
        std::fclose(f);
        path = kFontCandidates[i];
        break;
    }
    if (!path) {
        std::fprintf(stderr, "sim: no TTF found — keeping Montserrat (no PT accents)\n");
        return false;
    }

    lv_font_t *f13 = make(path, 13);
    lv_font_t *f14 = make(path, 14);
    lv_font_t *f16 = make(path, 16);
    lv_font_t *f20 = make(path, 20);
    lv_font_t *f40 = make(path, 40);
    if (!f13 || !f14 || !f16 || !f20 || !f40) {
        std::fprintf(stderr, "sim: FreeType create failed for %s\n", path);
        return false;
    }
    sim_font_13 = f13;
    sim_font_14 = f14;
    sim_font_16 = f16;
    sim_font_20 = f20;
    sim_font_40 = f40;
    std::printf("sim: font=%s\n", path);
    return true;
}
