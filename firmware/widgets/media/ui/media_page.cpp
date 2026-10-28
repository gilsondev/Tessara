#include "ui/media_page.h"

#include "fonts.h"
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
lv_obj_t *track_label = nullptr;
lv_obj_t *artist_label = nullptr;
lv_obj_t *cover_img = nullptr;
lv_event_cb_t back_cb = nullptr;
media_page::CmdFn cmd_cb = nullptr;
bool standalone_screen = false;
media::View cached = {};
bool cached_agent_ok = false;
bool had_good_agent = false;
lv_image_dsc_t cover_dsc[2] = {};
int cover_slot = 0;
char cover_shown_key[media::kCoverKeyMax] = {};

void bind_cover(lv_obj_t *img, const media::Cover &cover) {
    const bool same = std::strcmp(cover_shown_key, cover.key) == 0 && cover_dsc[cover_slot].data == cover.rgb565;
    if (same) return;
    if (cover_dsc[0].data) lv_image_cache_drop(&cover_dsc[0]);
    if (cover_dsc[1].data) lv_image_cache_drop(&cover_dsc[1]);
    cover_slot ^= 1;
    lv_image_dsc_t &dsc = cover_dsc[cover_slot];
    dsc = {};
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    dsc.header.w = cover.w;
    dsc.header.h = cover.h;
    dsc.header.stride = static_cast<uint16_t>(cover.w * 2);
    dsc.data_size = media::kCoverBytes;
    dsc.data = cover.rgb565;
    lv_image_set_src(img, &dsc);
    lv_obj_invalidate(img);
    std::strncpy(cover_shown_key, cover.key, sizeof(cover_shown_key) - 1);
    cover_shown_key[sizeof(cover_shown_key) - 1] = '\0';
}

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
    lv_label_set_text(title_label, "Música");

    body = kit::bare(parent);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(body, 16, 0);
    lv_obj_set_style_pad_top(body, 12, 0);
    lv_obj_set_style_pad_bottom(body, 16, 0);
    lv_obj_set_style_pad_row(body, 0, 0);

    meta = kit::bare(body);
    lv_obj_set_width(meta, lv_pct(100));
    lv_obj_set_height(meta, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(meta, 14, 0);

    // Large cover — matches MEDIA_COVER edge (200).
    cover_img = lv_image_create(meta);
    lv_obj_set_size(cover_img, media::kCoverEdge, media::kCoverEdge);
    lv_obj_add_flag(cover_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(cover_img, 12, 0);
    lv_obj_set_style_clip_corner(cover_img, true, 0);

    track_label = lv_label_create(meta);
    lv_label_set_text(track_label, "Nada tocando");
    lv_label_set_long_mode(track_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(track_label, lv_pct(100));
    lv_obj_set_style_text_align(track_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(track_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(track_label, theme::text_primary(), 0);

    artist_label = lv_label_create(meta);
    lv_label_set_text(artist_label, "");
    lv_label_set_long_mode(artist_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(artist_label, lv_pct(100));
    lv_obj_set_style_text_align(artist_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(artist_label, &font_pt15_bold, 0);
    lv_obj_set_style_text_color(artist_label, theme::text_secondary(), 0);
}

}  // namespace

void media_page::begin(lv_event_cb_t on_back, CmdFn on_cmd) {
    back_cb = on_back;
    cmd_cb = on_cmd;
}

void media_page::unmount() {
    if (!screen) return;
    if (standalone_screen) lv_obj_delete(screen);
    else lv_obj_clean(screen);
    screen = nullptr;
    title_label = nullptr;
    status_label = nullptr;
    body = meta = nullptr;
    track_label = artist_label = nullptr;
    cover_img = nullptr;
    cover_shown_key[0] = '\0';
    cover_dsc[0] = {};
    cover_dsc[1] = {};
    cover_slot = 0;
    standalone_screen = false;
}

void media_page::mount(lv_obj_t *parent) {
    if (!parent) return;
    unmount();
    screen = parent;
    standalone_screen = false;
    build_into(parent);
    // Prefer last good snapshot — never flash "sem Agente" if we had data.
    paint(cached, cached_agent_ok || had_good_agent);
    fire_cmd("?");
}

void media_page::show() {
    unmount();
    standalone_screen = true;
    screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    build_into(screen);
    paint(cached, cached_agent_ok || had_good_agent);
    fire_cmd("?");
}

void media_page::apply_theme() {
    if (!screen) return;
    lv_obj_set_style_bg_color(screen, theme::background(), 0);
    if (title_label) lv_obj_set_style_text_color(title_label, theme::text_primary(), 0);
    if (status_label) lv_obj_set_style_text_color(status_label, theme::text_secondary(), 0);
    if (track_label) lv_obj_set_style_text_color(track_label, theme::text_primary(), 0);
    if (artist_label) lv_obj_set_style_text_color(artist_label, theme::text_secondary(), 0);
    paint(cached, cached_agent_ok);
}

void media_page::paint(const media::View &view, bool agent_ok) {
    if (agent_ok) had_good_agent = true;
    cached = view;
    cached_agent_ok = agent_ok;
    if (!track_label) return;

    auto hide_cover = [&]() {
        if (cover_img) lv_obj_add_flag(cover_img, LV_OBJ_FLAG_HIDDEN);
        cover_shown_key[0] = '\0';
    };
    auto show_cover = [&]() {
        if (!cover_img || !view.cover.ready || !view.cover.rgb565) {
            hide_cover();
            return;
        }
        bind_cover(cover_img, view.cover);
        lv_obj_remove_flag(cover_img, LV_OBJ_FLAG_HIDDEN);
    };

    // Soft offline: keep last track if we already showed good data this session.
    if (!agent_ok && !had_good_agent) {
        set_if(track_label, "Aguardando Agente…");
        set_if(artist_label, "");
        set_status("");
        hide_cover();
        return;
    }
    if (!agent_ok && had_good_agent) {
        set_status("reconectando…");
        // Keep current labels/cover.
        return;
    }

    if (!view.ready || !view.media.ok) {
        set_if(track_label, view.ready ? "Nada tocando" : "Aguardando…");
        set_status((view.ready && !view.media.ok) ? "mídia indisponível" : "");
        set_if(artist_label, "");
        hide_cover();
        return;
    }

    if (view.media.state == media::MediaState::Stopped || !view.media.title[0]) {
        set_if(track_label, "Nada tocando");
        set_if(artist_label, "");
        set_status(view.stale ? "desatualizado" : "");
        hide_cover();
    } else {
        set_if(track_label, view.media.title);
        set_if(artist_label, view.media.artist);
        // Never show Spotify / Music / app name (spec).
        if (view.stale) set_status("desatualizado");
        else if (view.media.state == media::MediaState::Paused) set_status("pausado");
        else set_status("tocando");
        show_cover();
    }
}

const media::View &media_page::last_view() { return cached; }
