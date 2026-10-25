#include "ui/kit.h"

#include "domain/device_settings.h"
#include "ui/palette.h"
#include "ui/theme.h"
#include "ui/wifi_icons.h"

#include <src/core/lv_obj_event_private.h>
#include <src/core/lv_refr_private.h>
#include <src/display/lv_display_private.h>
#include <src/draw/lv_draw_private.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
constexpr int kDots = 8;

void prep_button(lv_obj_t *button, int pad_x, int height) {
    lv_obj_set_height(button, height);
    lv_obj_set_width(button, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_pad_hor(button, pad_x, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_outline_width(button, 0, 0);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(button, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

void spin(void *obj, int32_t value) {
    lv_obj_t *root = static_cast<lv_obj_t *>(obj);
    int phase = value % kDots;
    for (int i = 0; i < kDots; ++i) {
        int dist = (i - phase + kDots) % kDots;
        lv_opa_t opacity = LV_OPA_20;
        if (dist == 0) opacity = LV_OPA_COVER;
        else if (dist == 1 || dist == kDots - 1) opacity = LV_OPA_50;
        lv_obj_t *dot = lv_obj_get_child(root, i);
        if (dot) lv_obj_set_style_bg_opa(dot, opacity, 0);
    }
}

void cover(lv_event_t *event) {
    lv_cover_check_info_t *info = static_cast<lv_cover_check_info_t *>(lv_event_get_param(event));
    if (info->res == LV_COVER_RES_MASKED) return;
    lv_area_t coords;
    lv_obj_get_coords(static_cast<lv_obj_t *>(lv_event_get_current_target(event)), &coords);
    const lv_area_t *area = info->area;
    if (area->x1 >= coords.x1 && area->y1 >= coords.y1 && area->x2 <= coords.x2 && area->y2 <= coords.y2) {
        info->res = LV_COVER_RES_COVER;
    }
}
}  // namespace

lv_obj_t *kit::bare(lv_obj_t *parent) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t *kit::action_button(lv_obj_t *parent, const char *text, lv_event_cb_t on_click) {
    lv_obj_t *button = lv_button_create(parent);
    prep_button(button, 10, 30);
    lv_obj_set_style_bg_color(button, theme::primary(), 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_ext_click_area(button, 6);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(button, on_click, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &theme::label_font(), 0);
    lv_obj_set_style_text_color(label, theme::on_primary(), 0);
    return button;
}

lv_obj_t *kit::icon_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t on_click, uint32_t tone) {
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 36, 36);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(tone ? tone : palette::primary()), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_ext_click_area(button, 6);
    lv_obj_add_event_cb(button, on_click, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *glyph = lv_label_create(button);
    lv_label_set_text(glyph, symbol);
    lv_obj_set_style_text_font(glyph, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(glyph, tone ? lv_color_white() : theme::on_primary(), 0);
    lv_obj_center(glyph);
    return button;
}

lv_obj_t *kit::card(lv_obj_t *parent, const char *title, lv_event_cb_t on_click) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_width(box, lv_pct(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(box, theme::wash(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 16, 0);
    lv_obj_set_style_pad_hor(box, 16, 0);
    lv_obj_set_style_pad_ver(box, 14, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 4, 0);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    if (on_click) lv_obj_add_event_cb(box, on_click, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *name = lv_label_create(box);
    lv_label_set_text(name, title);
    lv_obj_set_style_text_font(name, &theme::title_font(), 0);
    lv_obj_set_style_text_color(name, theme::ink(), 0);
    lv_obj_t *detail = lv_label_create(box);
    lv_label_set_text(detail, "…");
    lv_obj_set_style_text_font(detail, &theme::body_font(), 0);
    lv_obj_set_style_text_color(detail, theme::muted(), 0);
    return box;
}

void kit::set_card_detail(lv_obj_t *card, const char *text) {
    if (!card || !text) return;
    lv_obj_t *detail = lv_obj_get_child(card, 1);
    if (detail && strcmp(lv_label_get_text(detail), text) != 0) lv_label_set_text(detail, text);
}

lv_obj_t *kit::battery(lv_obj_t *parent) {
    lv_obj_t *widget = bare(parent);
    lv_obj_set_size(widget, 28, 14);
    lv_obj_t *body = bare(widget);
    lv_obj_set_size(body, 24, 12);
    lv_obj_align(body, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(body, 3, 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_border_color(body, theme::ink(), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_t *fill = bare(body);
    lv_obj_set_size(fill, 0, 8);
    lv_obj_align(fill, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_radius(fill, 1, 0);
    lv_obj_set_style_bg_color(fill, theme::ink(), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_t *nub = bare(widget);
    lv_obj_set_size(nub, 2, 6);
    lv_obj_align(nub, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(nub, 1, 0);
    lv_obj_set_style_bg_color(nub, theme::ink(), 0);
    lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
    return widget;
}

void kit::set_battery(lv_obj_t *widget, int percent) {
    if (!widget) return;
    if (percent < 0) {
        lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *body = lv_obj_get_child(widget, 0);
    lv_obj_t *nub = lv_obj_get_child(widget, 1);
    lv_obj_t *fill = body ? lv_obj_get_child(body, 0) : nullptr;
    if (!body || !nub || !fill) return;
    lv_color_t juice = percent <= 15 ? theme::error() : theme::text_primary();
    lv_obj_set_style_bg_color(fill, juice, 0);
    if (percent > 100) percent = 100;
    int span = 18 * percent / 100;
    if (percent > 0 && span < 2) span = 2;
    lv_obj_set_width(fill, span);
}

lv_obj_t *kit::wifi(lv_obj_t *parent) {
    // Material Icons (Apache-2.0) embutidos — ver tools/gen_wifi_icons.py.
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_set_size(img, 20, 20);
    lv_image_set_src(img, wifi_icons::for_strength(WifiStrength::None));
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    return img;
}

void kit::set_wifi(lv_obj_t *widget, bool online, int rssi) {
    if (!widget) return;
    WifiStrength level = wifi_strength(online, rssi);
    lv_image_set_src(widget, wifi_icons::for_strength(level));
    lv_obj_set_style_image_recolor(widget, online ? theme::primary() : theme::muted(), 0);
    lv_obj_set_style_image_recolor_opa(widget, LV_OPA_COVER, 0);
}

lv_obj_t *kit::chip(lv_obj_t *parent, const char *text, bool selected, lv_event_cb_t on_click, void *user_data) {
    lv_obj_t *button = lv_button_create(parent);
    prep_button(button, 12, 30);
    lv_obj_set_style_radius(button, 15, 0);
    // Chain both axes so vertical drag on chips scrolls the parent page (settings Geral).
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(button, on_click, LV_EVENT_CLICKED, user_data);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &theme::label_font(), 0);
    set_chip_selected(button, selected);
    return button;
}

uint32_t chip_tone(lv_obj_t *chip) {
    uintptr_t stored = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(chip));
    return stored ? static_cast<uint32_t>(stored) : palette::primary();
}

void paint_chip_count(lv_obj_t *chip, bool selected) {
    lv_obj_t *badge = lv_obj_get_child(chip, 1);
    lv_obj_t *number = badge ? lv_obj_get_child(badge, 0) : nullptr;
    if (!badge || !number) return;
    lv_obj_set_style_bg_color(badge, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(badge, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    const char *text = lv_label_get_text(number);
    bool one = text && text[0] && !text[1];
    if (!selected) {
        lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    } else if (one) {
        lv_obj_set_size(badge, 22, 22);
    } else {
        lv_obj_set_size(badge, LV_SIZE_CONTENT, 22);
        lv_obj_set_style_min_width(badge, 22, 0);
        lv_obj_set_style_pad_hor(badge, 6, 0);
    }
    lv_obj_set_style_text_color(number, selected ? lv_color_hex(chip_tone(chip)) : theme::text_secondary(), 0);
    lv_obj_set_style_text_font(number, selected ? &font_pt15_bold : &theme::label_font(), 0);
    // The glyph sits above the line box's center. Nudge it so the ink matches the circle.
    lv_obj_set_style_translate_y(number, 1, 0);
}

void kit::set_chip_tone(lv_obj_t *chip, uint32_t rgb) {
    if (!chip || !rgb) return;
    lv_obj_set_user_data(chip, reinterpret_cast<void *>(static_cast<uintptr_t>(rgb)));
}

void kit::set_chip_selected(lv_obj_t *chip, bool selected, bool strong) {
    lv_obj_set_style_bg_color(chip, selected ? lv_color_hex(chip_tone(chip)) : theme::surface(), 0);
    lv_obj_set_style_border_width(chip, selected ? 0 : 1, 0);
    lv_obj_set_style_border_color(chip, theme::border(), 0);
    lv_obj_set_style_border_opa(chip, LV_OPA_COVER, 0);
    lv_obj_t *label = lv_obj_get_child(chip, 0);
    if (label) {
        bool platform = chip_tone(chip) == palette::primary();
        lv_color_t ink = selected ? (platform ? theme::on_primary() : lv_color_white()) : theme::text_primary();
        lv_obj_set_style_text_color(label, ink, 0);
        if (strong) {
            lv_obj_set_style_text_font(label, selected ? &font_pt15_bold : &theme::label_font(), 0);
            lv_obj_set_style_translate_y(label, 1, 0);
        }
    }
    paint_chip_count(chip, selected);
}

void kit::set_chip_count(lv_obj_t *chip, int count) {
    lv_obj_t *badge = lv_obj_get_child(chip, 1);
    if (count < 0) {
        if (badge) lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!badge) {
        badge = kit::bare(chip);
        lv_obj_set_style_margin_left(badge, 6, 0);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_label_create(badge);
    } else {
        lv_obj_remove_flag(badge, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *number = lv_obj_get_child(badge, 0);
    char text[8];
    snprintf(text, sizeof(text), "%d", count);
    if (strcmp(lv_label_get_text(number), text) != 0) lv_label_set_text(number, text);
    lv_color_t ink = lv_obj_get_style_text_color(lv_obj_get_child(chip, 0), 0);
    paint_chip_count(chip, lv_color_eq(ink, lv_color_white()));
}

kit::SubsysBar kit::subsys_bar(lv_obj_t *parent, const char *back_symbol, lv_event_cb_t on_back, uint32_t tone,
                               lv_event_cb_t on_action, const char *action_symbol) {
    SubsysBar bar = {};
    bar.root = bare(parent);
    lv_obj_set_width(bar.root, lv_pct(100));
    lv_obj_set_height(bar.root, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(bar.root, 0);
    lv_obj_set_style_pad_left(bar.root, 8, 0);
    lv_obj_set_style_pad_right(bar.root, 8, 0);
    lv_obj_set_style_pad_top(bar.root, 6, 0);
    lv_obj_set_style_pad_bottom(bar.root, 6, 0);
    lv_obj_set_style_pad_column(bar.root, 10, 0);
    lv_obj_set_flex_flow(bar.root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    icon_button(bar.root, back_symbol, on_back, tone);

    lv_obj_t *titles = bare(bar.root);
    lv_obj_set_height(titles, LV_SIZE_CONTENT);
    lv_obj_set_width(titles, 0);
    lv_obj_set_flex_grow(titles, 1);
    lv_obj_set_flex_flow(titles, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(titles, 0, 0);
    lv_obj_set_flex_align(titles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    bar.title = lv_label_create(titles);
    lv_label_set_text(bar.title, "");
    lv_obj_set_style_text_font(bar.title, &theme::title_font(), 0);
    lv_obj_set_style_text_color(bar.title, theme::text_primary(), 0);
    // Montserrat title metrics sit a hair high vs icon buttons; nudge down for optical center.
    lv_obj_set_style_translate_y(bar.title, 2, 0);

    bar.status = lv_label_create(titles);
    lv_label_set_text(bar.status, "");
    lv_label_set_long_mode(bar.status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(bar.status, lv_pct(100));
    lv_obj_set_style_text_font(bar.status, &theme::small_font(), 0);
    lv_obj_set_style_text_color(bar.status, theme::muted(), 0);
    lv_obj_add_flag(bar.status, LV_OBJ_FLAG_HIDDEN);

    if (on_action && action_symbol) bar.action = icon_button(bar.root, action_symbol, on_action, tone);
    return bar;
}

lv_obj_t *kit::spinner(lv_obj_t *parent, int32_t size, int32_t dot) {
    lv_obj_t *root = bare(parent);
    lv_obj_set_size(root, size, size);
    int32_t radius = (size - dot) / 2;
    for (int i = 0; i < kDots; ++i) {
        float angle = i * 2.f * 3.1415926f / kDots;
        lv_obj_t *child = bare(root);
        lv_obj_set_size(child, dot, dot);
        lv_obj_set_pos(child, radius + static_cast<int32_t>(lroundf(sinf(angle) * radius)),
                       radius - static_cast<int32_t>(lroundf(cosf(angle) * radius)));
        lv_obj_set_style_radius(child, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(child, theme::primary(), 0);
        lv_obj_set_style_bg_opa(child, LV_OPA_20, 0);
    }
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, root);
    lv_anim_set_exec_cb(&anim, spin);
    lv_anim_set_values(&anim, 0, kDots - 1);
    lv_anim_set_duration(&anim, 720);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_start(&anim);
    return root;
}

lv_obj_t *kit::opaque_image(lv_obj_t *parent, const void *src) {
    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(image, src);
    declare_opaque(image);
    return image;
}

void kit::declare_opaque(lv_obj_t *obj) { lv_obj_add_event_cb(obj, cover, LV_EVENT_COVER_CHECK, nullptr); }

void kit::draw_into(lv_obj_t *obj, lv_draw_buf_t *buf, const lv_area_t &buf_area, const lv_area_t &clip) {
    uint16_t px = lv_color_to_u16(theme::background());
    uint32_t stride = buf->header.stride;
    auto *base = static_cast<uint8_t *>(buf->data);
    int32_t w = lv_area_get_width(&clip);
    for (int32_t y = clip.y1; y <= clip.y2; ++y) {
        auto *row = reinterpret_cast<uint16_t *>(base + (y - buf_area.y1) * stride +
                                                 (clip.x1 - buf_area.x1) * sizeof(uint16_t));
        for (int32_t x = 0; x < w; ++x) row[x] = px;
    }

    lv_layer_t layer;
    lv_memzero(&layer, sizeof(layer));
    layer.draw_buf = buf;
    layer.buf_area = buf_area;
    layer.color_format = LV_COLOR_FORMAT_RGB565;
    layer._clip_area = clip;
    layer.phy_clip_area = clip;

    lv_display_t *disp = lv_obj_get_display(obj);
    lv_display_t *refreshing = lv_refr_get_disp_refreshing();
    lv_layer_t *head = disp->layer_head;
    disp->layer_head = &layer;
    lv_refr_set_disp_refreshing(disp);
    lv_obj_redraw(&layer, obj);
    while (layer.draw_task_head) {
        lv_draw_dispatch_wait_for_request();
        lv_draw_dispatch();
    }
    disp->layer_head = head;
    lv_refr_set_disp_refreshing(refreshing);
}

namespace {

int32_t glyph_width(const lv_font_t *font, const char *ch) {
    lv_point_t sz = {};
    lv_text_get_size(&sz, ch, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x > 0 ? sz.x : 0;
}

}  // namespace

kit::TabularTime kit::tabular_time(lv_obj_t *parent, const lv_font_t *font, lv_color_t color) {
    TabularTime clock = {};
    if (!parent || !font) return clock;

    int32_t digit_w = 0;
    for (char d = '0'; d <= '9'; ++d) {
        char one[2] = {d, '\0'};
        int32_t w = glyph_width(font, one);
        if (w > digit_w) digit_w = w;
    }
    // Placeholder "-" shares the digit slot (unsynced "--:--").
    {
        int32_t w = glyph_width(font, "-");
        if (w > digit_w) digit_w = w;
    }
    int32_t colon_w = glyph_width(font, ":");
    if (colon_w < 1) colon_w = digit_w / 3;
    clock.digit_w = digit_w;
    clock.colon_w = colon_w;

    clock.root = bare(parent);
    lv_obj_set_height(clock.root, LV_SIZE_CONTENT);
    lv_obj_set_width(clock.root, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(clock.root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(clock.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(clock.root, 0, 0);

    for (int i = 0; i < 8; ++i) {
        lv_obj_t *cell = lv_label_create(clock.root);
        lv_label_set_text(cell, "");
        lv_obj_set_style_text_font(cell, font, 0);
        lv_obj_set_style_text_color(cell, color, 0);
        lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(cell, digit_w);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
        clock.cells[i] = cell;
    }
    return clock;
}

void kit::tabular_time_set(TabularTime &clock, const char *text) {
    if (!clock.root || !text) return;
    if (std::strcmp(clock.shown, text) == 0) return;
    std::snprintf(clock.shown, sizeof(clock.shown), "%s", text);

    const int n = static_cast<int>(std::strlen(text));
    for (int i = 0; i < 8; ++i) {
        lv_obj_t *cell = clock.cells[i];
        if (!cell) continue;
        if (i >= n) {
            lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        char ch = text[i];
        char one[2] = {ch, '\0'};
        if (std::strcmp(lv_label_get_text(cell), one) != 0) lv_label_set_text(cell, one);
        const bool colon = ch == ':';
        lv_obj_set_width(cell, colon ? clock.colon_w : clock.digit_w);
        lv_obj_remove_flag(cell, LV_OBJ_FLAG_HIDDEN);
    }
}

void kit::tabular_time_recolor(TabularTime &clock, lv_color_t color) {
    for (int i = 0; i < 8; ++i) {
        if (clock.cells[i]) lv_obj_set_style_text_color(clock.cells[i], color, 0);
    }
}
