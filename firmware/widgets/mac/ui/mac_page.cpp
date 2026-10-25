#include "ui/mac_page.h"

#include "ui/kit.h"
#include "ui/palette.h"
#include "ui/theme.h"

#include <cstdio>
#include <cstring>

namespace {

lv_obj_t *screen = nullptr;
lv_obj_t *title_label = nullptr;
lv_obj_t *status_label = nullptr;
lv_obj_t *body = nullptr;
lv_obj_t *meta = nullptr;
lv_obj_t *controls = nullptr;
lv_obj_t *track_label = nullptr;
lv_obj_t *artist_label = nullptr;
lv_obj_t *app_label = nullptr;
lv_obj_t *btn_prev = nullptr;
lv_obj_t *btn_play = nullptr;
lv_obj_t *btn_next = nullptr;
lv_event_cb_t back_cb = nullptr;
mac_page::CmdFn cmd_cb = nullptr;
bool standalone_screen = false;
mac::View cached = {};
bool cached_agent_ok = false;

void set_if(lv_obj_t *label, const char *text) {
    if (label && text && strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

void set_status(const char *text) {
    if (!status_label) return;
    if (!text || !text[0]) {
        lv_label_set_text(status_label, "");
        lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    set_if(status_label, text);
    lv_obj_remove_flag(status_label, LV_OBJ_FLAG_HIDDEN);
}

void fire_cmd(const char *cmd) {
    if (cmd_cb && cmd) cmd_cb(cmd);
}

void on_prev(lv_event_t *) {
    set_status("‹ anterior");
    fire_cmd("PREV");
}
void on_play(lv_event_t *) {
    set_status("play/pause…");
    fire_cmd("PLAYPAUSE");
}
void on_next(lv_event_t *) {
    set_status("próxima ›");
    fire_cmd("NEXT");
}

lv_obj_t *make_ctrl(lv_obj_t *row, const char *symbol, lv_event_cb_t cb) {
    lv_obj_t *btn = kit::icon_button(row, symbol, cb, palette::widget_mac());
    lv_obj_set_size(btn, 64, 52);
    lv_obj_set_ext_click_area(btn, 16);
    // Ensure the glyph never steals the tap from the button.
    lv_obj_t *lab = lv_obj_get_child(btn, 0);
    if (lab) {
        lv_obj_clear_flag(lab, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(lab, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
    return btn;
}

void style_controls() {
    auto paint_btn = [](lv_obj_t *btn) {
        if (!btn) return;
        lv_obj_set_style_bg_color(btn, lv_color_hex(palette::widget_mac()), 0);
        lv_obj_t *lab = lv_obj_get_child(btn, 0);
        if (lab) lv_obj_set_style_text_color(lab, lv_color_white(), 0);
    };
    paint_btn(btn_prev);
    paint_btn(btn_play);
    paint_btn(btn_next);
}

void build_into(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, theme::background(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_pad_row(parent, 0, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(parent, lv_pct(100), lv_pct(100));

    kit::SubsysBar bar = kit::subsys_bar(parent, LV_SYMBOL_LEFT, back_cb, palette::widget_mac());
    title_label = bar.title;
    status_label = bar.status;
    lv_label_set_text(title_label, "Mac");

    body = kit::bare(parent);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(body, 20, 0);
    lv_obj_set_style_pad_top(body, 24, 0);
    lv_obj_set_style_pad_bottom(body, 12, 0);
    lv_obj_set_style_pad_row(body, 0, 0);

    meta = kit::bare(body);
    lv_obj_set_width(meta, lv_pct(100));
    lv_obj_set_flex_grow(meta, 1);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(meta, 8, 0);

    track_label = lv_label_create(meta);
    lv_label_set_text(track_label, "Nada tocando");
    lv_label_set_long_mode(track_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(track_label, lv_pct(100));
    lv_obj_set_style_text_align(track_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(track_label, &theme::title_font(), 0);
    lv_obj_set_style_text_color(track_label, theme::text_primary(), 0);

    artist_label = lv_label_create(meta);
    lv_label_set_text(artist_label, "");
    lv_label_set_long_mode(artist_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(artist_label, lv_pct(100));
    lv_obj_set_style_text_align(artist_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(artist_label, &theme::label_font(), 0);
    lv_obj_set_style_text_color(artist_label, theme::text_secondary(), 0);

    app_label = lv_label_create(meta);
    lv_label_set_text(app_label, "");
    lv_obj_set_style_text_font(app_label, &theme::small_font(), 0);
    lv_obj_set_style_text_color(app_label, theme::text_secondary(), 0);

    // Pin controls to the bottom of the page so title wrap never covers them.
    controls = kit::bare(body);
    lv_obj_set_width(controls, lv_pct(100));
    lv_obj_set_height(controls, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(controls, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(controls, 20, 0);
    lv_obj_set_style_pad_top(controls, 12, 0);
    lv_obj_set_style_pad_bottom(controls, 8, 0);
    lv_obj_add_flag(controls, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(controls);

    btn_prev = make_ctrl(controls, LV_SYMBOL_PREV, on_prev);
    btn_play = make_ctrl(controls, LV_SYMBOL_PLAY, on_play);
    btn_next = make_ctrl(controls, LV_SYMBOL_NEXT, on_next);
}

}  // namespace

void mac_page::begin(lv_event_cb_t on_back, CmdFn on_cmd) {
    back_cb = on_back;
    cmd_cb = on_cmd;
}

void mac_page::unmount() {
    if (!screen) return;
    if (standalone_screen) lv_obj_delete(screen);
    else lv_obj_clean(screen);
    screen = nullptr;
    title_label = nullptr;
    status_label = nullptr;
    body = meta = controls = nullptr;
    track_label = artist_label = app_label = nullptr;
    btn_prev = btn_play = btn_next = nullptr;
    standalone_screen = false;
}

void mac_page::mount(lv_obj_t *parent) {
    if (!parent) return;
    unmount();
    screen = parent;
    standalone_screen = false;
    build_into(parent);
    if (!cmd_cb) set_status("sem handler");
    else fire_cmd("?");
}

void mac_page::show() {
    unmount();
    standalone_screen = true;
    screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    build_into(screen);
    fire_cmd("?");
}

void mac_page::apply_theme() {
    if (!screen) return;
    lv_obj_set_style_bg_color(screen, theme::background(), 0);
    if (title_label) lv_obj_set_style_text_color(title_label, theme::text_primary(), 0);
    if (status_label) lv_obj_set_style_text_color(status_label, theme::text_secondary(), 0);
    if (track_label) lv_obj_set_style_text_color(track_label, theme::text_primary(), 0);
    if (artist_label) lv_obj_set_style_text_color(artist_label, theme::text_secondary(), 0);
    if (app_label) lv_obj_set_style_text_color(app_label, theme::text_secondary(), 0);
    style_controls();
    paint(cached, cached_agent_ok);
}

void mac_page::paint(const mac::View &view, bool agent_ok) {
    cached = view;
    cached_agent_ok = agent_ok;
    if (!track_label) return;

    if (!agent_ok) {
        set_if(track_label, "Mac offline");
        set_if(artist_label, "");
        set_if(app_label, "");
        set_status("");
        if (btn_play) {
            lv_obj_t *lab = lv_obj_get_child(btn_play, 0);
            if (lab) lv_label_set_text(lab, LV_SYMBOL_PLAY);
        }
        return;
    }

    if (!view.ready || !view.media.ok) {
        set_if(track_label, "Nada tocando");
        set_status((view.ready && !view.media.ok) ? "mídia indisponível" : "");
        set_if(artist_label, "");
        set_if(app_label, "");
        return;
    }

    if (view.media.state == mac::MediaState::Stopped || !view.media.title[0]) {
        set_if(track_label, "Nada tocando");
        set_if(artist_label, "");
        set_if(app_label, "");
        set_status(view.stale ? "desatualizado" : "");
    } else {
        set_if(track_label, view.media.title);
        set_if(artist_label, view.media.artist);
        set_if(app_label, view.media.app);
        if (view.stale) set_status("desatualizado");
        else if (view.media.state == mac::MediaState::Paused) set_status("pausado");
        else set_status("tocando");
    }

    if (btn_play) {
        lv_obj_t *lab = lv_obj_get_child(btn_play, 0);
        if (lab) {
            lv_label_set_text(lab, view.media.state == mac::MediaState::Playing ? LV_SYMBOL_PAUSE
                                                                              : LV_SYMBOL_PLAY);
        }
    }
}

const mac::View &mac_page::last_view() { return cached; }
