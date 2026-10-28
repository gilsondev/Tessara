#include "ui/brand_mark.h"
#include "ui/tile_icons.h"
#include "ui/home.h"

#include "calendar/snapshot.h"
#include "clock_summary.h"
#include "domain/dashboard_layout.h"
#include "domain/home_tile_size.h"
#include "mac/focus_snapshot.h"
#include "platform/daytime.h"
#include "platform/face_prefs.h"
#include "platform/power.h"
#include "platform/settings.h"
#include "platform/widget_settings.h"
#include "tessera/runtime.h"
#include "todoist/snapshot.h"
#include "ui/aitokens_logos.h"
#include "ui/kit.h"
#include "ui/ts_kit.h"
#include "ui/calendar_icons.h"
#include "ui/weather_icons.h"
#include "weather/codes.h"
#include "ui/palette.h"
#include "ui/theme.h"

using tessera::ui::TsBare;
using tessera::ui::TsIconButton;
using tessera::ui::TsSetBattery;
using tessera::ui::TsSetWifi;
using tessera::ui::TsSpinner;
using tessera::ui::TsWifi;

#include <cstdio>
#include <cstring>

namespace {
lv_obj_t *screen = nullptr;
lv_obj_t *wordmark = nullptr;
lv_obj_t *link_icon = nullptr;
lv_obj_t *gear = nullptr;
kit::TabularTime clock_face_ui = {};
lv_obj_t *date_label = nullptr;
lv_obj_t *battery_widget = nullptr;
lv_obj_t *stage = nullptr;
lv_obj_t *hero = nullptr;
lv_obj_t *grid = nullptr;
lv_obj_t *tasks_tile = nullptr;
lv_obj_t *task_icon = nullptr;
lv_obj_t *task_count = nullptr;
lv_obj_t *task_spinner = nullptr;
lv_obj_t *hello_label = nullptr;
lv_obj_t *weather_tile = nullptr;
lv_obj_t *weather_icon = nullptr;
lv_obj_t *weather_glyph = nullptr;
lv_obj_t *weather_spinner = nullptr;
lv_obj_t *weather_detail = nullptr;
lv_obj_t *aitokens_tile = nullptr;
lv_obj_t *aitokens_icon = nullptr;
lv_obj_t *aitokens_alert = nullptr;
lv_obj_t *aitokens_empty = nullptr;
lv_obj_t *aitokens_line_codex = nullptr;
lv_obj_t *aitokens_line_claude = nullptr;
lv_obj_t *aitokens_line_cursor = nullptr;
lv_obj_t *calendar_tile = nullptr;
lv_obj_t *calendar_icon = nullptr;
lv_obj_t *calendar_glyph = nullptr;
lv_obj_t *calendar_body = nullptr;
// One line per event the panel can hold, so the day is not cut at the tile size.
lv_obj_t *calendar_lines[calendar::kMaxEvents] = {};
// Legacy single-line pointer kept for theme paths that still mention it.
lv_obj_t *calendar_detail = nullptr;
lv_obj_t *pair = nullptr;  // Música + Mac side-by-side (same band as Tarefas/Clima).
lv_obj_t *mac_tile = nullptr;
lv_obj_t *mac_icon = nullptr;
lv_obj_t *mac_detail = nullptr;
bool linked = false;
int link_rssi = 0;
lv_event_cb_t open_widget_cb = nullptr;
bool hero_show_clock = true;
bool hero_show_date = true;
bool hero_show_greeting = true;
uint8_t hero_layout_gen = 0;
lv_timer_t *clock_timer = nullptr;

void clear_tile_ptrs() {
    tasks_tile = nullptr;
    task_icon = nullptr;
    task_count = nullptr;
    task_spinner = nullptr;
    weather_tile = nullptr;
    weather_icon = nullptr;
    weather_glyph = nullptr;
    weather_spinner = nullptr;
    weather_detail = nullptr;
    aitokens_tile = nullptr;
    aitokens_icon = nullptr;
    aitokens_alert = nullptr;
    aitokens_empty = nullptr;
    aitokens_line_codex = nullptr;
    aitokens_line_claude = nullptr;
    aitokens_line_cursor = nullptr;
    calendar_tile = nullptr;
    calendar_icon = nullptr;
    calendar_glyph = nullptr;
    calendar_body = nullptr;
    for (auto &line : calendar_lines) line = nullptr;
    calendar_detail = nullptr;
    pair = nullptr;
    mac_tile = nullptr;
    mac_icon = nullptr;
    mac_detail = nullptr;
}

void set_if_changed(lv_obj_t *label, const char *text) {
    if (label && text && strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

lv_obj_t *tile(lv_obj_t *parent, const char *symbol, lv_color_t tone, const char *name, lv_obj_t **icon_out,
              lv_event_cb_t on_click, void *user_data = nullptr) {
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
    if (on_click) {
        lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(box, LV_OBJ_FLAG_SCROLL_CHAIN);  // drag on tile scrolls Home
        lv_obj_add_event_cb(box, on_click, LV_EVENT_CLICKED, user_data);
    }
    lv_obj_t *slot = TsBare(box);
    lv_obj_set_size(slot, 24, 24);
    lv_obj_t *icon = lv_label_create(slot);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, tone, 0);
    lv_obj_center(icon);
    if (icon_out) *icon_out = icon;
    lv_obj_t *title = lv_label_create(box);
    lv_label_set_text(title, name);
    lv_obj_set_style_text_font(title, &theme::label_font(), 0);
    lv_obj_set_style_text_color(title, theme::text_primary(), 0);
    return box;
}

void paint_card(lv_obj_t *box) {
    if (!box) return;
    lv_obj_set_style_bg_color(box, theme::surface(), 0);
    lv_obj_set_style_border_color(box, theme::border(), 0);
}

void cache_hero_flags(const dashboard::Layout &layout) {
    hero_show_clock = dashboard::is_enabled(layout, dashboard::id_clock());
    hero_show_date = dashboard::is_enabled(layout, dashboard::id_date());
    hero_show_greeting = dashboard::is_enabled(layout, dashboard::id_greeting());
    hero_layout_gen++;
}

void paint_clock(lv_timer_t *) {
    // Poll often; only commit a new civil second ~once per real second so a 1 s
    // LVGL timer (or an NTP step) cannot flash 1→2→3 as a glitch.
    static uint8_t beat = 0;
    if (beat++ % 25 == 0) home::set_battery(power::battery_percent());

    int hour = 0, minute = 0, second = 0, wday = 0, day = 0, month = 0, year = 0;
    const bool ok = daytime::local(hour, minute, second, wday, day, month, year);
    clock_face::Options opts = face_prefs::options();
    const bool want_seconds = opts.time == clock_face::TimeFormat::Hms;

    static int last_h = -1, last_m = -1, last_s = -1, last_day = -1;
    static bool last_ok = false;
    static uint8_t last_fmt = 0xff;
    static uint8_t last_layout_gen = 0xff;
    static uint32_t last_sec_ms = 0;
    const uint8_t fmt = static_cast<uint8_t>(opts.time);
    const bool same_face = last_layout_gen == hero_layout_gen && ok == last_ok && hour == last_h &&
                           minute == last_m && day == last_day && fmt == last_fmt &&
                           (!want_seconds || second == last_s);
    if (same_face) return;

    if (want_seconds && last_s >= 0 && second != last_s && hour == last_h && minute == last_m &&
        day == last_day && ok == last_ok && fmt == last_fmt && last_layout_gen == hero_layout_gen) {
        // Natural second (or NTP) moved on — wait ~1 Hz of wall time before painting.
        const uint32_t now_ms = lv_tick_get();
        if (last_sec_ms != 0 && (now_ms - last_sec_ms) < 900) return;
    }

    last_h = hour;
    last_m = minute;
    last_s = second;
    last_day = day;
    last_ok = ok;
    last_fmt = fmt;
    last_layout_gen = hero_layout_gen;
    if (want_seconds) last_sec_ms = lv_tick_get();

    opts.second = second;
    clock_face::Summary face = clock_face::from_parts(hour, minute, wday, day, month, year, ok, opts);
    const char *time = hero_show_clock ? face.time : "";
    const char *date = hero_show_date ? face.date_short : "";
    const char *hello = hero_show_greeting ? face.greeting : "";
    if (hero_show_clock && !face.synced) time = face.time;
    home::set_clock(time, date, hello);
}

void replace_glyph_with_image(lv_obj_t **icon_out, const lv_image_dsc_t &dsc, lv_color_t tone) {
    if (!icon_out || !*icon_out) return;
    lv_obj_t *slot = lv_obj_get_parent(*icon_out);
    lv_obj_delete(*icon_out);
    *icon_out = lv_image_create(slot);
    lv_image_set_src(*icon_out, &dsc);
    lv_obj_set_size(*icon_out, 24, 24);
    lv_obj_set_style_image_recolor(*icon_out, tone, 0);
    lv_obj_set_style_image_recolor_opa(*icon_out, LV_OPA_COVER, 0);
    lv_obj_center(*icon_out);
}

void add_todoist_tile(lv_event_cb_t on_open) {
    tasks_tile = tile(grid, " ", theme::widget_todoist(), "Tarefas", &task_icon, on_open,
                      const_cast<char *>(dashboard::id_todoist()));
    replace_glyph_with_image(&task_icon, tile_icons::todoist(), theme::widget_todoist());
    task_count = lv_label_create(tasks_tile);
    lv_label_set_text(task_count, "");
    lv_obj_set_style_text_font(task_count, &theme::title_font(), 0);
    lv_obj_set_style_text_color(task_count, theme::text_primary(), 0);
    lv_obj_add_flag(task_count, LV_OBJ_FLAG_HIDDEN);
    // Spinner stays until every visible list has its tasks. The number replaces it.
    task_spinner = TsSpinner(tasks_tile, 28, 4);
}

void add_weather_tile(lv_event_cb_t on_open) {
    weather_tile = tile(grid, LV_SYMBOL_TINT, theme::widget_weather(), "Clima", &weather_icon, on_open,
                        const_cast<char *>(dashboard::id_weather()));
    if (weather_icon) lv_obj_add_flag(weather_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *weather_slot = weather_tile ? lv_obj_get_child(weather_tile, 0) : nullptr;
    if (weather_slot) {
        constexpr int kGlyph = 32;
        constexpr int32_t kScale = (kGlyph * 256) / 96;
        lv_obj_set_size(weather_slot, kGlyph, kGlyph);
        weather_glyph = lv_image_create(weather_slot);
        lv_image_set_src(weather_glyph, weather_icons::unavailable());
        lv_image_set_scale(weather_glyph, kScale);
        lv_obj_set_size(weather_glyph, kGlyph, kGlyph);
        lv_obj_center(weather_glyph);
        weather_spinner = TsSpinner(weather_slot, kGlyph, 5);
        lv_obj_center(weather_spinner);
        lv_obj_add_flag(weather_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    weather_detail = lv_label_create(weather_tile);
    lv_label_set_text(weather_detail, "definir local");
    lv_label_set_long_mode(weather_detail, LV_LABEL_LONG_DOT);
    lv_obj_set_width(weather_detail, lv_pct(100));
    lv_obj_set_style_text_font(weather_detail, &theme::small_font(), 0);
    lv_obj_set_style_text_color(weather_detail, theme::text_secondary(), 0);
}

void add_aitokens_tile(lv_event_cb_t on_open) {
    aitokens_tile = TsBare(stage);
    lv_obj_set_width(aitokens_tile, lv_pct(100));
    lv_obj_set_height(aitokens_tile, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(aitokens_tile, theme::surface(), 0);
    lv_obj_set_style_bg_opa(aitokens_tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(aitokens_tile, 16, 0);
    lv_obj_set_style_border_width(aitokens_tile, 1, 0);
    lv_obj_set_style_border_color(aitokens_tile, theme::border(), 0);
    lv_obj_set_style_pad_all(aitokens_tile, 10, 0);
    lv_obj_set_flex_flow(aitokens_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(aitokens_tile, 2, 0);
    if (on_open) {
        lv_obj_add_flag(aitokens_tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(aitokens_tile, LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_add_event_cb(aitokens_tile, on_open, LV_EVENT_CLICKED, const_cast<char *>(dashboard::id_aitokens()));
    }
    lv_obj_t *ai_head = TsBare(aitokens_tile);
    lv_obj_set_width(ai_head, lv_pct(100));
    lv_obj_set_height(ai_head, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ai_head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ai_head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ai_head, 8, 0);
    aitokens_icon = lv_label_create(ai_head);
    lv_label_set_text(aitokens_icon, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_font(aitokens_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(aitokens_icon, theme::widget_aitokens(), 0);
    lv_obj_t *ai_title = lv_label_create(ai_head);
    lv_label_set_text(ai_title, "AI Tokens");
    lv_obj_set_style_text_font(ai_title, &theme::label_font(), 0);
    lv_obj_set_style_text_color(ai_title, theme::text_primary(), 0);
    lv_obj_set_flex_grow(ai_title, 1);
    aitokens_alert = lv_label_create(ai_head);
    lv_label_set_text(aitokens_alert, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(aitokens_alert, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(aitokens_alert, theme::warning(), 0);
    lv_obj_add_flag(aitokens_alert, LV_OBJ_FLAG_HIDDEN);
    aitokens_empty = lv_label_create(aitokens_tile);
    lv_label_set_text(aitokens_empty, "ligar Bridge");
    lv_obj_set_style_text_font(aitokens_empty, &theme::small_font(), 0);
    lv_obj_set_style_text_color(aitokens_empty, theme::text_secondary(), 0);
    auto make_fonte_row = [](lv_obj_t *parent, const lv_image_dsc_t *logo, const char *fallback,
                             lv_obj_t **label_out) {
        lv_obj_t *row = TsBare(parent);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        if (logo) {
            lv_obj_t *mark = lv_image_create(row);
            lv_image_set_src(mark, logo);
            lv_obj_set_size(mark, 16, 16);
            lv_image_set_scale(mark, (16 * 256) / 24);
        }
        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text(label, fallback);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(label, 1);
        lv_obj_set_style_text_font(label, &theme::small_font(), 0);
        lv_obj_set_style_text_color(label, theme::text_secondary(), 0);
        if (label_out) *label_out = label;
    };
    make_fonte_row(aitokens_tile, aitokens_logos::codex(), "Codex", &aitokens_line_codex);
    make_fonte_row(aitokens_tile, aitokens_logos::claude(), "Claude", &aitokens_line_claude);
    make_fonte_row(aitokens_tile, aitokens_logos::cursor(), "Cursor", &aitokens_line_cursor);
}

home_tile::Size read_calendar_tile_size() {
    char buf[8] = {};
    if (widget_settings::get(dashboard::id_calendar(), home_tile::kSettingKey, buf, sizeof(buf))) {
        return home_tile::parse(buf);
    }
    return home_tile::Size::M;
}

void apply_calendar_tile_height() {
    const home_tile::Size size = read_calendar_tile_size();
    if (calendar_tile) {
        lv_obj_set_style_min_height(calendar_tile, home_tile::tile_min_height_px(size), 0);
    }
    if (calendar_body) {
        lv_obj_set_style_min_height(calendar_body, home_tile::body_min_height_px(size), 0);
    }
}

void add_calendar_tile(lv_event_cb_t on_open) {
    // Full-width on stage (same band as AI Tokens) — not in the Tarefas/Clima row.
    calendar_tile = TsBare(stage);
    lv_obj_set_width(calendar_tile, lv_pct(100));
    lv_obj_set_height(calendar_tile, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(calendar_tile, theme::surface(), 0);
    lv_obj_set_style_bg_opa(calendar_tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(calendar_tile, 16, 0);
    lv_obj_set_style_border_width(calendar_tile, 1, 0);
    lv_obj_set_style_border_color(calendar_tile, theme::border(), 0);
    lv_obj_set_style_pad_all(calendar_tile, 10, 0);
    lv_obj_set_flex_flow(calendar_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(calendar_tile, 2, 0);
    lv_obj_remove_flag(calendar_tile, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    if (on_open) {
        lv_obj_add_flag(calendar_tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(calendar_tile, LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_add_event_cb(calendar_tile, on_open, LV_EVENT_CLICKED,
                            const_cast<char *>(dashboard::id_calendar()));
    }

    lv_obj_t *head = TsBare(calendar_tile);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 8, 0);

    constexpr int kGlyph = 24;
    lv_obj_t *slot = TsBare(head);
    lv_obj_set_size(slot, kGlyph, kGlyph);
    calendar_glyph = lv_image_create(slot);
    lv_image_set_src(calendar_glyph, calendar_icons::month());
    lv_obj_set_style_image_recolor(calendar_glyph, theme::widget_calendar(), 0);
    lv_obj_set_style_image_recolor_opa(calendar_glyph, LV_OPA_COVER, 0);
    lv_obj_set_size(calendar_glyph, kGlyph, kGlyph);
    lv_obj_center(calendar_glyph);
    calendar_icon = nullptr;

    lv_obj_t *title = lv_label_create(head);
    lv_label_set_text(title, "Calendário");
    lv_obj_set_style_text_font(title, &theme::label_font(), 0);
    lv_obj_set_style_text_color(title, theme::text_primary(), 0);
    lv_obj_set_flex_grow(title, 1);

    calendar_body = TsBare(calendar_tile);
    lv_obj_set_width(calendar_body, lv_pct(100));
    lv_obj_set_height(calendar_body, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(calendar_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(calendar_body, 2, 0);

    for (int i = 0; i < calendar::kMaxEvents; ++i) {
        calendar_lines[i] = lv_label_create(calendar_body);
        lv_label_set_text(calendar_lines[i], i == 0 ? "ligar agenda" : "");
        lv_label_set_long_mode(calendar_lines[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(calendar_lines[i], lv_pct(100));
        // Flex otherwise keeps the full text width and the line paints past the card.
        lv_obj_set_style_min_width(calendar_lines[i], 0, 0);
        lv_obj_set_style_text_font(calendar_lines[i], &theme::small_font(), 0);
        lv_obj_set_style_text_color(calendar_lines[i], theme::text_secondary(), 0);
        if (i > 0) lv_obj_add_flag(calendar_lines[i], LV_OBJ_FLAG_HIDDEN);
    }
    calendar_detail = calendar_lines[0];
    apply_calendar_tile_height();
}

void ensure_pair_row() {
    if (pair || !stage) return;
    pair = TsBare(stage);
    lv_obj_set_width(pair, lv_pct(100));
    lv_obj_set_height(pair, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pair, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pair, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(pair, 8, 0);
}

// Half-width card in `pair` — same proportions as Tarefas/Clima in `grid`.
void add_pair_tile(lv_obj_t **tile_out, lv_obj_t **icon_out, lv_obj_t **detail_out, const char *symbol,
                   lv_color_t tone, const char *title, const char *detail, const char *widget_id,
                   lv_event_cb_t on_open) {
    ensure_pair_row();
    *tile_out = tile(pair, symbol, tone, title, icon_out, on_open,
                     widget_id ? const_cast<char *>(widget_id) : nullptr);
    if (*tile_out) kit::press_feedback(*tile_out);
    *detail_out = lv_label_create(*tile_out);
    lv_label_set_text(*detail_out, detail);
    lv_label_set_long_mode(*detail_out, LV_LABEL_LONG_DOT);
    lv_obj_set_width(*detail_out, lv_pct(100));
    lv_obj_set_style_text_font(*detail_out, &theme::small_font(), 0);
    lv_obj_set_style_text_color(*detail_out, theme::text_secondary(), 0);
}

void add_mac_tile(lv_event_cb_t on_open) {
    add_pair_tile(&mac_tile, &mac_icon, &mac_detail, " ", theme::widget_mac(), "Mac", "Nenhum",
                  dashboard::id_mac(), on_open);
    // Notebook icon — never Tessera mark on this tile (spec 2026-10-26).
    replace_glyph_with_image(&mac_icon, tile_icons::notebook(), theme::widget_mac());
}

void add_generic_tile(const char *title, const char *widget_id) {
    lv_obj_t *unused = nullptr;
    tile(grid, LV_SYMBOL_DIRECTORY, theme::text_secondary(), title ? title : "Widget", &unused, open_widget_cb,
         widget_id ? const_cast<char *>(widget_id) : nullptr);
}

void add_registered_id(const char **registered, int &reg_n, const char *id) {
    if (!id || !id[0] || reg_n >= tessera::kMaxWidgetTypes) return;
    for (int i = 0; i < reg_n; ++i) {
        if (strcmp(registered[i], id) == 0) return;
    }
    registered[reg_n++] = id;
}

void mount_enabled_tiles(const dashboard::Layout &layout) {
    cache_hero_flags(layout);
    // Compose = enabled ∩ (registry ∪ shell tiles). A package with draws_tile
    // paints its own card. The shell list covers the sim, which does not link
    // every factory.
    const char *registered[tessera::kMaxWidgetTypes];
    int reg_n = 0;
    auto &rt = tessera::runtime();
    for (int i = 0; i < rt.type_count(); ++i) add_registered_id(registered, reg_n, rt.type_id(i));
    const char *shell_ids[] = {
        dashboard::id_greeting(), dashboard::id_date(),     dashboard::id_todoist(),
        dashboard::id_weather(),  dashboard::id_aitokens(), dashboard::id_calendar(),
        dashboard::id_mac(),      dashboard::id_clock(),
    };
    for (const char *id : shell_ids) add_registered_id(registered, reg_n, id);
    char slots[dashboard::kMaxSlots][dashboard::kIdLen];
    int n = dashboard::compose(layout, registered, reg_n, slots, dashboard::kMaxSlots);
    for (int i = 0; i < n; ++i) {
        const char *id = slots[i];
        if (rt.type_draws_tile(id)) {
            ensure_pair_row();
            rt.mount(id, pair, tessera::Surface::Tile);
            continue;
        }
        // Package declares tile kind (None = hero-only / no grid).
        tessera::HomeTile kind = rt.type_home_tile(id);
        if (kind == tessera::HomeTile::None) {
            // Hero-only (saudação/data/relógio) — sem tile na grade.
            if (strcmp(id, dashboard::id_greeting()) == 0 || strcmp(id, dashboard::id_date()) == 0 ||
                strcmp(id, dashboard::id_clock()) == 0)
                continue;
            // Fallback when registry was empty at compose time (sim).
            if (strcmp(id, dashboard::id_todoist()) == 0) kind = tessera::HomeTile::Todoist;
            else if (strcmp(id, dashboard::id_weather()) == 0) kind = tessera::HomeTile::Weather;
            else if (strcmp(id, dashboard::id_aitokens()) == 0) kind = tessera::HomeTile::AiTokens;
            else if (strcmp(id, dashboard::id_calendar()) == 0) kind = tessera::HomeTile::Calendar;
            else if (strcmp(id, dashboard::id_mac()) == 0) kind = tessera::HomeTile::Mac;
            else kind = tessera::HomeTile::Generic;
        }
        switch (kind) {
            case tessera::HomeTile::Todoist:
                add_todoist_tile(open_widget_cb);
                break;
            case tessera::HomeTile::Weather:
                add_weather_tile(open_widget_cb);
                break;
            case tessera::HomeTile::AiTokens:
                add_aitokens_tile(open_widget_cb);
                break;
            case tessera::HomeTile::Calendar:
                add_calendar_tile(open_widget_cb);
                break;
            case tessera::HomeTile::Mac:
                add_mac_tile(open_widget_cb);
                break;
            case tessera::HomeTile::Generic: {
                const char *title = rt.type_title(rt.find_registered(id));
                if (!title || !title[0]) title = id;
                add_generic_tile(title, id);
                break;
            }
            case tessera::HomeTile::None:
                break;
            default:
                break;
        }
    }
}
}  // namespace

void home::rebuild(const dashboard::Layout &layout) {
    if (!stage || !grid) return;
    // Drop package tiles before their parent row is deleted.
    tessera::runtime().unmount_surface(tessera::Surface::Tile);
    // AI Tokens + Calendar stay full-width on `stage`. Música+Mac share a side-by-side `pair` row.
    if (aitokens_tile) {
        lv_obj_delete(aitokens_tile);
        aitokens_tile = nullptr;
    }
    if (calendar_tile) {
        lv_obj_delete(calendar_tile);
        calendar_tile = nullptr;
    }
    if (pair) {
        lv_obj_delete(pair);
        pair = nullptr;
        mac_tile = mac_icon = mac_detail = nullptr;
    }
    lv_obj_clean(grid);
    clear_tile_ptrs();
    mount_enabled_tiles(layout);
    arrange();
    apply_theme();
}

void home::begin(lv_event_cb_t on_open_widget, lv_event_cb_t on_settings) {
    open_widget_cb = on_open_widget;
    tessera::runtime().set_tile_open(reinterpret_cast<tessera::EventFn>(on_open_widget));
    screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, theme::background(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(screen, &theme::title_font(), 0);
    lv_obj_set_style_text_color(screen, theme::text_primary(), 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_pad_row(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *body = TsBare(screen);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_hor(body, 10, 0);
    lv_obj_set_style_pad_top(body, 6, 0);
    lv_obj_set_style_pad_bottom(body, 8, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 6, 0);

    lv_obj_t *top = TsBare(body);
    lv_obj_set_width(top, lv_pct(100));
    lv_obj_set_height(top, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    wordmark = lv_image_create(top);
    lv_image_set_src(wordmark, &tessera_lockup(palette::mode() == palette::Mode::Dark));
    // Native lockup is 108×30 RGB565A8 (Logo Kit v1, transparent).
    lv_obj_set_size(wordmark, 108, 30);

    lv_obj_t *status = TsBare(top);
    lv_obj_set_height(status, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 6, 0);

    link_icon = TsWifi(status);
    lv_obj_add_flag(link_icon, LV_OBJ_FLAG_HIDDEN);

    // USB-only boards still bias the BAT ADC; never create the icon until pack-detect exists.
    battery_widget = nullptr;
    gear = TsIconButton(status, LV_SYMBOL_SETTINGS, on_settings);

    stage = TsBare(body);
    lv_obj_set_width(stage, lv_pct(100));
    lv_obj_set_flex_grow(stage, 1);
    lv_obj_set_flex_flow(stage, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(stage, 6, 0);
    lv_obj_set_style_pad_bottom(stage, 16, 0);
    kit::enable_list_scroll(stage);  // hero + tiles overflow 480px — vertical scroll

    hero = TsBare(stage);
    lv_obj_set_width(hero, lv_pct(100));
    lv_obj_set_height(hero, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hero, 8, 0);

    lv_obj_t *when = TsBare(hero);
    lv_obj_set_height(when, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(when, 1);
    lv_obj_set_flex_flow(when, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(when, 0, 0);

    hello_label = lv_label_create(when);
    lv_label_set_text(hello_label, "");
    lv_obj_set_style_text_font(hello_label, &theme::label_font(), 0);
    lv_obj_set_style_text_color(hello_label, theme::text_secondary(), 0);

    date_label = lv_label_create(when);
    lv_label_set_text(date_label, "");
    lv_obj_set_style_text_font(date_label, &theme::small_font(), 0);
    lv_obj_set_style_text_color(date_label, theme::text_secondary(), 0);

    // Tabular slots: proportional Montserrat digits must not shift the hero each second.
    clock_face_ui = kit::tabular_time(hero, &theme::display_font(), theme::text_primary());
    // paint_clock() fills at once; avoid a "--:--" flash before the first tick.
    kit::tabular_time_set(clock_face_ui, "  :  ");
    if (clock_face_ui.root) {
        lv_obj_add_flag(clock_face_ui.root, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(clock_face_ui.root, LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_set_ext_click_area(clock_face_ui.root, 8);
        if (open_widget_cb) {
            lv_obj_add_event_cb(clock_face_ui.root, open_widget_cb, LV_EVENT_CLICKED,
                                const_cast<char *>(dashboard::id_clock()));
        }
    }

    grid = TsBare(stage);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(grid, 8, 0);

    mount_enabled_tiles(settings::load_dashboard());

    arrange();
    // Sub-second poll catches every civil second; paint_clock rate-limits the digits.
    if (!clock_timer) clock_timer = lv_timer_create(paint_clock, 200, nullptr);
    paint_clock(nullptr);
}

void home::show() {
    if (screen) lv_screen_load(screen);
}

void home::arrange() {
    if (!stage || !grid) return;
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(grid, 0);
}

void home::set_summary(int total, bool pending) {
    if (pending) {
        if (task_count) lv_obj_add_flag(task_count, LV_OBJ_FLAG_HIDDEN);
        if (task_spinner) lv_obj_remove_flag(task_spinner, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (task_spinner) lv_obj_add_flag(task_spinner, LV_OBJ_FLAG_HIDDEN);
    if (!task_count) return;
    todoist::Summary summary = {};
    summary.total = total;
    summary.pending = false;
    char text[16];
    todoist::format_home_count(summary, text, sizeof(text));
    lv_obj_remove_flag(task_count, LV_OBJ_FLAG_HIDDEN);
    set_if_changed(task_count, text);
}

void home::set_mac(const focus::View &view, bool agent_ok) {
    if (!mac_detail) return;
    char text[72];
    if (!agent_ok) {
        snprintf(text, sizeof(text), "Mac offline");
    } else if (!view.ready || !view.ok) {
        snprintf(text, sizeof(text), "%s%s", view.ready ? "Focus indisponível" : "Aguardando…",
                 view.stale ? " · desatualizado" : "");
    } else if (!view.active_id[0]) {
        snprintf(text, sizeof(text), "Nenhum%s", view.stale ? " · desatualizado" : "");
    } else {
        snprintf(text, sizeof(text), "%s%s", view.active_name[0] ? view.active_name : view.active_id,
                 view.stale ? " · desatualizado" : "");
    }
    set_if_changed(mac_detail, text);
    if (mac_icon) {
        lv_obj_set_style_image_recolor(mac_icon, agent_ok ? theme::widget_mac() : theme::error(), 0);
        lv_obj_set_style_image_recolor_opa(mac_icon, LV_OPA_COVER, 0);
    }
}

void home::set_calendar(const calendar::View &view) {
    if (!calendar_lines[0]) return;
    apply_calendar_tile_height();

    auto show_status = [&](const char *text) {
        set_if_changed(calendar_lines[0], text);
        lv_obj_clear_flag(calendar_lines[0], LV_OBJ_FLAG_HIDDEN);
        for (int i = 1; i < calendar::kMaxEvents; ++i) {
            if (calendar_lines[i]) lv_obj_add_flag(calendar_lines[i], LV_OBJ_FLAG_HIDDEN);
        }
    };

    // Nearest day with events — today when it still has any, otherwise the next one, so the tile
    // keeps answering "what's next" instead of going blank every evening.
    const calendar::Event *next[calendar::kMaxEvents];
    int next_n = 0;
    int next_day = -1;
    int calendar_day = 0;
    int calendar_month = 0;
    int calendar_year = 0;
    if (view.ready) {
        next_day = calendar::next_day(view);
        int idx[calendar::kMaxEvents];
        const int n = next_day >= 0 ? calendar::events_on_day(view, next_day, idx, calendar::kMaxEvents) : 0;
        for (int i = 0; i < n; ++i) next[next_n++] = &view.events[idx[i]];
        int h = 0, m = 0, s = 0, wday = 0;
        if (!daytime::local(h, m, s, wday, calendar_day, calendar_month, calendar_year)) {
            calendar_day = 0;
            calendar_month = 0;
            calendar_year = 0;
        }
    }

    if (!view.bridge_ok && !view.ready) {
        show_status("sem conexão");
    } else if (!view.linked && !view.ready) {
        // Never had a real CALENDAR reply — ask to link. Cached ready keeps events below.
        show_status("ligar agenda");
    } else if (!view.ready) {
        show_status("carregando…");
    } else if (next_n <= 0) {
        char text[40];
        if (!view.linked) {
            snprintf(text, sizeof(text), "ligar agenda");
        } else {
            snprintf(text, sizeof(text), "livre%s", view.stale ? " · desatualizado" : "");
        }
        show_status(text);
    } else {
        const int show = next_n < calendar::kMaxEvents ? next_n : calendar::kMaxEvents;
        for (int i = 0; i < calendar::kMaxEvents; ++i) {
            if (!calendar_lines[i]) continue;
            if (i >= show) {
                lv_obj_add_flag(calendar_lines[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            const calendar::Event &e = *next[i];
            // Today has no date. Tomorrow is "Amanhã:"; later days are "DD/MM:".
            char day[16];
            calendar::day_prefix(day, sizeof(day), e.day_offset, calendar_day, calendar_month,
                                 calendar_year);
            char time_text[16];
            if (e.all_day) {
                snprintf(time_text, sizeof(time_text), day[0] ? "dia todo" : "Dia todo");
            } else {
                snprintf(time_text, sizeof(time_text), "%02d:%02d", e.start_min / 60, e.start_min % 60);
            }
            char text[80];
            if (day[0]) {
                snprintf(text, sizeof(text), "%s %s · %s", day, time_text,
                         e.title[0] ? e.title : "Evento");
            } else {
                snprintf(text, sizeof(text), "%s · %s", time_text, e.title[0] ? e.title : "Evento");
            }
            set_if_changed(calendar_lines[i], text);
            lv_obj_clear_flag(calendar_lines[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (calendar_glyph) {
        const lv_image_dsc_t *src = (!view.bridge_ok && !view.ready) ? calendar_icons::warn()
                                  : (view.linked && view.ready && next_n <= 0) ? calendar_icons::busy()
                                                                               : calendar_icons::month();
        lv_image_set_src(calendar_glyph, src);
        lv_color_t tone = (!view.bridge_ok && !view.ready) ? theme::error() : theme::widget_calendar();
        lv_obj_set_style_image_recolor(calendar_glyph, tone, 0);
        lv_obj_set_style_image_recolor_opa(calendar_glyph, LV_OPA_COVER, 0);
    }
}

void home::set_weather(const weather::View &view) {
    if (!weather_detail) return;
    char text[56];
    constexpr int kGlyph = 32;
    constexpr int32_t kScale = (kGlyph * 256) / 96;
    auto show_spin = [](bool on) {
        if (weather_spinner) {
            if (on) lv_obj_remove_flag(weather_spinner, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(weather_spinner, LV_OBJ_FLAG_HIDDEN);
        }
        if (weather_glyph) {
            if (on) lv_obj_add_flag(weather_glyph, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_remove_flag(weather_glyph, LV_OBJ_FLAG_HIDDEN);
        }
    };
    auto set_glyph = [&](const lv_image_dsc_t *src) {
        if (!weather_glyph || !src) return;
        lv_image_set_src(weather_glyph, src);
        lv_image_set_scale(weather_glyph, kScale);
        lv_obj_set_size(weather_glyph, kGlyph, kGlyph);
        lv_obj_center(weather_glyph);
    };
    if (!view.placed) {
        snprintf(text, sizeof(text), "definir local");
        show_spin(false);
        set_glyph(weather_icons::unavailable());
    } else if (view.failed && !view.ready) {
        snprintf(text, sizeof(text), "falhou");
        show_spin(false);
        set_glyph(weather_icons::unavailable());
    } else if (!view.ready) {
        snprintf(text, sizeof(text), "carregando…");
        show_spin(true);
    } else {
        snprintf(text, sizeof(text), "%d° · %s%s", view.forecast.temperature_c, weather::phrase(view.forecast.code),
                 view.stale ? " · desatualizado" : "");
        show_spin(false);
        set_glyph(weather_icons::for_code(view.forecast.code));
    }
    set_if_changed(weather_detail, text);
}

void home::set_aitokens(const aitokens::View &view) {
    DeviceSettings device = settings::load_device();
    int worst = 100;  // remaining %; lower = worse
    bool any = false;
    auto set_row = [&](const char *name, const char *id, lv_obj_t *label, bool show) {
        lv_obj_t *row = label ? lv_obj_get_parent(label) : nullptr;
        if (!show) {
            if (row) lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        if (row) lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);

        const aitokens::Source *src = aitokens::find(view, id);
        const bool ok = view.ready && src && src->available && src->display[0];
        if (!ok) {
            // Enabled but offline / no live data — keep the row; never hide solely for available=0.
            char out[48];
            if (!view.ready) snprintf(out, sizeof(out), "%s · …", name);
            else if (src && src->display[0] && strcmp(src->display, "—") != 0)
                snprintf(out, sizeof(out), "%s · %s", name, src->display);
            else
                snprintf(out, sizeof(out), "%s · offline", name);
            set_if_changed(label, out);
            if (label) lv_obj_set_style_text_color(label, theme::text_secondary(), 0);
            any = true;
            return;
        }

        // value is remaining percent for Codex, Claude and Cursor.
        char out[80];
        aitokens::RowLevel level = aitokens::RowLevel::Ok;
        const int rem = aitokens::home_row_text(name, *src, view.stale, out, sizeof(out), &level);
        any = true;
        if (rem < worst) worst = rem;

        lv_color_t tone = theme::text_secondary();
        if (level == aitokens::RowLevel::Alert) tone = theme::error();
        else if (level == aitokens::RowLevel::Low) tone = theme::warning();
        set_if_changed(label, out);
        if (label) lv_obj_set_style_text_color(label, tone, 0);
    };
    set_row("Codex", "codex", aitokens_line_codex, device.show_aitokens_codex);
    set_row("Claude", "claude", aitokens_line_claude, device.show_aitokens_claude);
    set_row("Cursor", "cursor", aitokens_line_cursor, device.show_aitokens_cursor);

    // Optional order of fonte rows (widget setting sources_order CSV).
    if (aitokens_tile) {
        char order_buf[48] = {};
        const char *ids[3] = {"codex", "claude", "cursor"};
        if (widget_settings::get(dashboard::id_aitokens(), "sources_order", order_buf, sizeof(order_buf)) &&
            order_buf[0]) {
            int n = 0;
            char *p = order_buf;
            while (*p && n < 3) {
                while (*p == ' ' || *p == ',') ++p;
                if (!*p) break;
                char *start = p;
                while (*p && *p != ',') ++p;
                char save = *p;
                *p = '\0';
                if (!strcmp(start, "codex") || !strcmp(start, "claude") || !strcmp(start, "cursor")) {
                    ids[n++] = start;
                }
                *p = save;
                if (*p == ',') ++p;
            }
        }
        auto row_of = [](const char *id) -> lv_obj_t * {
            lv_obj_t *label = nullptr;
            if (!strcmp(id, "codex")) label = aitokens_line_codex;
            else if (!strcmp(id, "claude")) label = aitokens_line_claude;
            else if (!strcmp(id, "cursor")) label = aitokens_line_cursor;
            return label ? lv_obj_get_parent(label) : nullptr;
        };
        for (int i = 0; i < 3; ++i) {
            lv_obj_t *row = row_of(ids[i]);
            if (!row) continue;
            const uint32_t cnt = lv_obj_get_child_cnt(aitokens_tile);
            if (cnt > 0) lv_obj_move_to_index(row, static_cast<int32_t>(cnt - 1));
        }
    }

    if (aitokens_empty) {
        if (!any) {
            set_if_changed(aitokens_empty, view.ready ? "nenhuma fonte" : "ligar Bridge");
            lv_obj_set_style_text_color(aitokens_empty, theme::text_secondary(), 0);
            lv_obj_remove_flag(aitokens_empty, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(aitokens_empty, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (aitokens_tile) {
        lv_color_t border = theme::border();
        int width = 1;
        if (any && worst < aitokens::kRemainAlert) {
            border = theme::error();
            width = 2;
        } else if (any && worst < aitokens::kRemainLow) {
            border = theme::warning();
            width = 2;
        }
        lv_obj_set_style_border_color(aitokens_tile, border, 0);
        lv_obj_set_style_border_width(aitokens_tile, width, 0);
    }
    if (aitokens_alert) {
        if (any && worst < aitokens::kRemainLow) {
            lv_obj_set_style_text_color(aitokens_alert,
                                        worst < aitokens::kRemainAlert ? theme::error() : theme::warning(), 0);
            lv_obj_remove_flag(aitokens_alert, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(aitokens_alert, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void home::apply_theme() {
    if (!screen) return;
    lv_obj_set_style_bg_color(screen, theme::background(), 0);
    lv_obj_set_style_text_color(screen, theme::text_primary(), 0);
    if (wordmark) {
        const lv_image_dsc_t &light = tessera_lockup(false);
        const lv_image_dsc_t &dark = tessera_lockup(true);
        lv_image_cache_drop(&light);
        lv_image_cache_drop(&dark);
        lv_image_set_src(wordmark, palette::mode() == palette::Mode::Dark ? &dark : &light);
        lv_obj_invalidate(wordmark);
    }
    if (gear) {
        lv_obj_set_style_bg_color(gear, theme::primary(), 0);
        lv_obj_t *glyph = lv_obj_get_child(gear, 0);
        if (glyph) lv_obj_set_style_text_color(glyph, theme::on_primary(), 0);
    }
    if (hello_label) lv_obj_set_style_text_color(hello_label, theme::text_secondary(), 0);
    kit::tabular_time_recolor(clock_face_ui, theme::text_primary());
    if (date_label) lv_obj_set_style_text_color(date_label, theme::text_secondary(), 0);
    if (link_icon) TsSetWifi(link_icon, linked, link_rssi);
    if (task_icon) {
        lv_obj_set_style_image_recolor(task_icon, theme::widget_todoist(), 0);
        lv_obj_set_style_image_recolor_opa(task_icon, LV_OPA_COVER, 0);
    }
    if (task_count) lv_obj_set_style_text_color(task_count, theme::text_primary(), 0);
    if (weather_icon) lv_obj_set_style_text_color(weather_icon, theme::widget_weather(), 0);
    // Weather glyphs: soft recolor for contrast on light/dark surfaces.
    if (weather_glyph) {
        const bool dark = palette::mode() == palette::Mode::Dark;
        lv_obj_set_style_image_recolor(weather_glyph, dark ? lv_color_hex(0xE8F4FF) : lv_color_hex(0x1A4A6E), 0);
        lv_obj_set_style_image_recolor_opa(weather_glyph, dark ? LV_OPA_40 : LV_OPA_30, 0);
    }
    if (weather_detail) lv_obj_set_style_text_color(weather_detail, theme::text_secondary(), 0);
    if (calendar_glyph) {
        lv_obj_set_style_image_recolor(calendar_glyph, theme::widget_calendar(), 0);
        lv_obj_set_style_image_recolor_opa(calendar_glyph, LV_OPA_COVER, 0);
    }
    for (auto *line : calendar_lines) {
        if (line) lv_obj_set_style_text_color(line, theme::text_secondary(), 0);
    }
    if (aitokens_icon) lv_obj_set_style_text_color(aitokens_icon, theme::widget_aitokens(), 0);
    tessera::runtime().notify_theme();
    if (mac_icon) {
        lv_obj_set_style_image_recolor(mac_icon, theme::widget_mac(), 0);
        lv_obj_set_style_image_recolor_opa(mac_icon, LV_OPA_COVER, 0);
    }
    if (mac_detail) lv_obj_set_style_text_color(mac_detail, theme::text_secondary(), 0);
    paint_card(tasks_tile);
    paint_card(weather_tile);
    paint_card(calendar_tile);
    paint_card(mac_tile);
    // Só o fundo do card AI Tokens — borda/cores de alerta vêm de set_aitokens.
    if (aitokens_tile) lv_obj_set_style_bg_color(aitokens_tile, theme::surface(), 0);
    auto recolor_name = [](lv_obj_t *card) {
        lv_obj_t *name = card ? lv_obj_get_child(card, 1) : nullptr;
        if (name) lv_obj_set_style_text_color(name, theme::text_primary(), 0);
    };
    recolor_name(tasks_tile);
    recolor_name(weather_tile);
    recolor_name(calendar_tile);
    recolor_name(mac_tile);
    if (aitokens_tile) {
        lv_obj_t *head = lv_obj_get_child(aitokens_tile, 0);
        lv_obj_t *title = head ? lv_obj_get_child(head, 1) : nullptr;
        if (title) lv_obj_set_style_text_color(title, theme::text_primary(), 0);
    }
}

void home::set_clock(const char *time, const char *date, const char *hello) {
    if (time) kit::tabular_time_set(clock_face_ui, time);
    set_if_changed(date_label, date);
    set_if_changed(hello_label, hello ? hello : "");
}

void home::set_link(bool online, int rssi) {
    linked = online;
    link_rssi = rssi;
    if (!link_icon) return;
    lv_obj_remove_flag(link_icon, LV_OBJ_FLAG_HIDDEN);
    TsSetWifi(link_icon, online, rssi);
}

void home::set_battery(int percent) {
    if (battery_widget) TsSetBattery(battery_widget, percent);
}
