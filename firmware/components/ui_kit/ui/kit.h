#pragma once

#include <lvgl.h>

// Small LVGL building blocks shared by the screens.
namespace kit {

// Plain container: no theme style, no scrolling, not clickable.
lv_obj_t *bare(lv_obj_t *parent);
// Phone-like press: slight scale + dim, ~100ms ease_out; larger hit pad.
void press_feedback(lv_obj_t *obj);
// Vertical list with momentum + elastic overscroll (calendar / weather / settings).
void enable_list_scroll(lv_obj_t *obj);
lv_obj_t *action_button(lv_obj_t *parent, const char *text, lv_event_cb_t on_click);
// Symbol button. tone 0 uses the platform primary; a widget passes its own color.
lv_obj_t *icon_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t on_click, uint32_t tone = 0);
// Outline of a cell. percent is 0..100. Below zero hides the icon.
lv_obj_t *battery(lv_obj_t *parent);
void set_battery(lv_obj_t *widget, int percent);
// Fan Wi-Fi: 3 arcos + ponto. rssi em dBm (só vale se online).
lv_obj_t *wifi(lv_obj_t *parent);
void set_wifi(lv_obj_t *widget, bool online, int rssi);
lv_obj_t *chip(lv_obj_t *parent, const char *text, bool selected, lv_event_cb_t on_click, void *user_data);
// `strong` draws the label in bold while the chip is selected. Settings chips leave it off.
void set_chip_selected(lv_obj_t *chip, bool selected, bool strong = false);
// Selected fill and the count ink. 0 keeps the platform primary.
void set_chip_tone(lv_obj_t *chip, uint32_t rgb);
// Count beside the name. Below zero hides it. On the red chip it sits in a white circle.
void set_chip_count(lv_obj_t *chip, int count);
// Ring of dots chasing each other; the animation dies with the object.
lv_obj_t *spinner(lv_obj_t *parent, int32_t size, int32_t dot);

// Shared product chrome: back · title (+ status) · optional action.
struct SubsysBar {
    lv_obj_t *root;
    lv_obj_t *title;
    lv_obj_t *status;
    lv_obj_t *action;
};
SubsysBar subsys_bar(lv_obj_t *parent, const char *back_symbol, lv_event_cb_t on_back, uint32_t tone,
                     lv_event_cb_t on_action = nullptr, const char *action_symbol = nullptr);
// Home card. Child 0 is the title, child 1 is the detail line.
lv_obj_t *card(lv_obj_t *parent, const char *title, lv_event_cb_t on_click);
void set_card_detail(lv_obj_t *card, const char *text);
// Image that tells LVGL it fully hides what is behind it, so nothing underneath is drawn.
lv_obj_t *opaque_image(lv_obj_t *parent, const void *src);
// Same promise for any object whose children paint every pixel of it.
void declare_opaque(lv_obj_t *obj);
// Renders `obj` into `buf` over the theme background. Pixel (0, 0) sits at (buf_area.x1, buf_area.y1).
void draw_into(lv_obj_t *obj, lv_draw_buf_t *buf, const lv_area_t &buf_area, const lv_area_t &clip);

// Fixed-advance clock text (Montserrat digits vary — "1" vs "0" must not nudge neighbors).
// Up to 8 cells for "HH:MM:SS". `root` is the clickable/flex host.
struct TabularTime {
    lv_obj_t *root = nullptr;
    lv_obj_t *cells[8] = {};
    int32_t digit_w = 0;
    int32_t colon_w = 0;
    char shown[12] = {};
};
TabularTime tabular_time(lv_obj_t *parent, const lv_font_t *font, lv_color_t color);
void tabular_time_set(TabularTime &clock, const char *text);
void tabular_time_recolor(TabularTime &clock, lv_color_t color);

}  // namespace kit
