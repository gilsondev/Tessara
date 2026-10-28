#include "ui/media_tile.h"

#include "ui/kit.h"
#include "ui/theme.h"
#include "ui/ts_kit.h"

#include <cstdio>
#include <cstring>

using tessera::ui::TsBare;

namespace {

constexpr char kId[] = "org.tessera.media";

lv_obj_t *card = nullptr;
lv_obj_t *icon = nullptr;
lv_obj_t *detail = nullptr;
media::View last = {};
bool have = false;
bool last_agent_ok = false;

void set_if_changed(lv_obj_t *label, const char *text) {
    if (label && text && std::strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

lv_obj_t *make_card(lv_obj_t *parent, lv_event_cb_t on_open) {
    lv_obj_t *box = TsBare(parent);
    lv_obj_set_width(box, 0);
    lv_obj_set_height(box, 108);
    lv_obj_set_flex_grow(box, 1);
    lv_obj_set_style_min_width(box, 0, 0);
    lv_obj_set_style_bg_color(box, theme::surface(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 16, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, theme::border(), 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(box, 2, 0);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_SCROLL_CHAIN);
    if (on_open) lv_obj_add_event_cb(box, on_open, LV_EVENT_CLICKED, const_cast<char *>(kId));

    lv_obj_t *slot = TsBare(box);
    lv_obj_set_size(slot, 24, 24);
    icon = lv_label_create(slot);
    lv_label_set_text(icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xAF52DE), 0);
    lv_obj_center(icon);

    lv_obj_t *title = lv_label_create(box);
    lv_label_set_text(title, "Música");
    lv_obj_set_style_text_font(title, &theme::label_font(), 0);
    lv_obj_set_style_text_color(title, theme::text_primary(), 0);
    kit::press_feedback(box);
    return box;
}

}  // namespace

bool media_tile::claims(const char *widget_id) { return widget_id && std::strcmp(widget_id, kId) == 0; }

void media_tile::add(lv_obj_t *row, lv_event_cb_t on_open) {
    if (!row) return;
    card = make_card(row, on_open);
    detail = lv_label_create(card);
    lv_label_set_text(detail, "Nada tocando");
    lv_label_set_long_mode(detail, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(detail, 4000, 0);
    lv_obj_set_width(detail, lv_pct(100));
    lv_obj_set_style_text_font(detail, &theme::small_font(), 0);
    lv_obj_set_style_text_color(detail, theme::text_secondary(), 0);
    if (have) set(last, last_agent_ok);
}

void media_tile::set(const media::View &view, bool agent_ok) {
    last = view;
    last_agent_ok = agent_ok;
    have = true;
    if (!detail) return;
    char text[72];
    if (!agent_ok) {
        std::snprintf(text, sizeof(text), "sem Agente");
    } else if (!view.ready || !view.media.ok || view.media.state == media::MediaState::Stopped ||
               !view.media.title[0]) {
        std::snprintf(text, sizeof(text), "Nada tocando%s", view.stale ? " · desatualizado" : "");
    } else if (view.media.artist[0]) {
        std::snprintf(text, sizeof(text), "%s · %s%s%s", view.media.title, view.media.artist,
                      view.media.state == media::MediaState::Paused ? " · pausado" : "",
                      view.stale ? " · desatualizado" : "");
    } else {
        std::snprintf(text, sizeof(text), "%s%s%s", view.media.title,
                      view.media.state == media::MediaState::Paused ? " · pausado" : "",
                      view.stale ? " · desatualizado" : "");
    }
    set_if_changed(detail, text);
    if (icon) lv_obj_set_style_text_color(icon, agent_ok ? lv_color_hex(0xAF52DE) : theme::error(), 0);
}

void media_tile::apply_theme() {
    if (card) {
        lv_obj_set_style_bg_color(card, theme::surface(), 0);
        lv_obj_set_style_border_color(card, theme::border(), 0);
        lv_obj_t *title = lv_obj_get_child(card, 1);
        if (title) lv_obj_set_style_text_color(title, theme::text_primary(), 0);
    }
    if (detail) lv_obj_set_style_text_color(detail, theme::text_secondary(), 0);
    if (have) set(last, last_agent_ok);
    else if (icon) lv_obj_set_style_text_color(icon, lv_color_hex(0xAF52DE), 0);
}

void media_tile::detach() {
    card = nullptr;
    icon = nullptr;
    detail = nullptr;
}
