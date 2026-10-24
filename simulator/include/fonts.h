#pragma once

#include <lvgl.h>

// Runtime FreeType faces (Portuguese glyphs). Filled by sim_fonts::init().
extern const lv_font_t *sim_font_13;
extern const lv_font_t *sim_font_14;
extern const lv_font_t *sim_font_16;
extern const lv_font_t *sim_font_20;
extern const lv_font_t *sim_font_40;

#define font_pt18 (*sim_font_16)
#define font_pt16 (*sim_font_14)
#define font_pt15 (*sim_font_14)
#define font_pt15_bold (*sim_font_14)
#define font_pt13 (*sim_font_13)
