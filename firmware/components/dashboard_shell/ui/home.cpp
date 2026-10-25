#include "ui/brand_mark.h"
#include "ui/home.h"

#include "clock_summary.h"
#include "domain/dashboard_layout.h"
#include "platform/daytime.h"
#include "platform/face_prefs.h"
#include "platform/power.h"
#include "platform/settings.h"
#include "tessara/runtime.h"
#include "todoist/snapshot.h"
#include "ui/aitokens_logos.h"
#include "ui/kit.h"
#include "ui/ts_kit.h"
#include "ui/weather_icons.h"
#include "weather/codes.h"
#include "ui/palette.h"
#include "ui/theme.h"

using tessara::ui::TsBare;
using tessara::ui::TsIconButton;
using tessara::ui::TsSetBattery;
using tessara::ui::TsSetWifi;
using tessara::ui::TsSpinner;
using tessara::ui::TsWifi;

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
bool linked = false;
int link_rssi = 0;
lv_event_cb_t open_widget_cb = nullptr;
bool hero_show_clock = true;
bool hero_show_date = true;
bool hero_show_greeting = true;
uint8_t hero_layout_gen = 0;
lv_timer_t *clock_timer = nullptr;

constexpr int kAiWarnPct = 50;
constexpr int kAiCriticalPct = 20;

void clear_tile_ptrs() {
    tasks_tile = nullptr;
    task_icon = nullptr;
    task_count = nullptr;
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
    static uint8_t beat = 0;
    if (beat++ % 20 == 0) home::set_battery(power::battery_percent());

    int hour = 0, minute = 0, second = 0, wday = 0, day = 0, month = 0, year = 0;
    const bool ok = daytime::local(hour, minute, second, wday, day, month, year);
    clock_face::Options opts = face_prefs::options();
    const bool want_seconds = opts.time == clock_face::TimeFormat::Hms;

    // HH:MM only needs a paint on minute (or day/sync/format) change — skip the other 59 ticks.
    // HH:MM:SS paints each second, but never reloads NVS / dashboard here.
    static int last_h = -1, last_m = -1, last_s = -1, last_day = -1;
    static bool last_ok = false;
    static uint8_t last_fmt = 0xff;
    static uint8_t last_layout_gen = 0xff;
    const uint8_t fmt = static_cast<uint8_t>(opts.time);
    if (last_layout_gen == hero_layout_gen && ok == last_ok && hour == last_h && minute == last_m &&
        day == last_day && fmt == last_fmt && (!want_seconds || second == last_s)) {
        return;
    }
    last_h = hour;
    last_m = minute;
    last_s = second;
    last_day = day;
    last_ok = ok;
    last_fmt = fmt;
    last_layout_gen = hero_layout_gen;

    opts.second = second;
    clock_face::Summary face = clock_face::from_parts(hour, minute, wday, day, month, year, ok, opts);
    const char *time = hero_show_clock ? face.time : "";
    const char *date = hero_show_date ? face.date_short : "";
    const char *hello = hero_show_greeting ? face.greeting : "";
    if (hero_show_clock && !face.synced) time = face.time;
    home::set_clock(time, date, hello);
}

void add_todoist_tile(lv_event_cb_t on_open) {
    tasks_tile = tile(grid, LV_SYMBOL_LIST, theme::widget_todoist(), "Tarefas", &task_icon, on_open,
                      const_cast<char *>(dashboard::id_todoist()));
    task_count = lv_label_create(tasks_tile);
    lv_label_set_text(task_count, "…");
    lv_obj_set_style_text_font(task_count, &theme::title_font(), 0);
    lv_obj_set_style_text_color(task_count, theme::text_primary(), 0);
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

void add_generic_tile(const char *title, const char *widget_id) {
    lv_obj_t *unused = nullptr;
    tile(grid, LV_SYMBOL_DIRECTORY, theme::text_secondary(), title ? title : "Widget", &unused, open_widget_cb,
         widget_id ? const_cast<char *>(widget_id) : nullptr);
}

void mount_enabled_tiles(const dashboard::Layout &layout) {
    cache_hero_flags(layout);
    // Compose = enabled ∩ runtime registry (new widgets appear without editing this list).
    const char *registered[tessara::kMaxWidgetTypes];
    int reg_n = 0;
    for (int i = 0; i < tessara::runtime().type_count() && reg_n < tessara::kMaxWidgetTypes; ++i) {
        const char *id = tessara::runtime().type_id(i);
        if (id && id[0]) registered[reg_n++] = id;
    }
    if (reg_n == 0) {
        // Registry not ready yet (sim can call home before register_builtin).
        registered[reg_n++] = dashboard::id_greeting();
        registered[reg_n++] = dashboard::id_date();
        registered[reg_n++] = dashboard::id_todoist();
        registered[reg_n++] = dashboard::id_weather();
        registered[reg_n++] = dashboard::id_aitokens();
        registered[reg_n++] = dashboard::id_clock();
    }
    char slots[dashboard::kMaxSlots][dashboard::kIdLen];
    int n = dashboard::compose(layout, registered, reg_n, slots, dashboard::kMaxSlots);
    for (int i = 0; i < n; ++i) {
        const char *id = slots[i];
        auto &rt = tessara::runtime();
        // Package declares tile kind (None = hero-only / no grid).
        tessara::HomeTile kind = rt.type_home_tile(id);
        if (kind == tessara::HomeTile::None) {
            // Hero-only (saudação/data/relógio) — sem tile na grade.
            if (strcmp(id, dashboard::id_greeting()) == 0 || strcmp(id, dashboard::id_date()) == 0 ||
                strcmp(id, dashboard::id_clock()) == 0)
                continue;
            // Fallback when registry was empty at compose time (sim).
            if (strcmp(id, dashboard::id_todoist()) == 0) kind = tessara::HomeTile::Todoist;
            else if (strcmp(id, dashboard::id_weather()) == 0) kind = tessara::HomeTile::Weather;
            else if (strcmp(id, dashboard::id_aitokens()) == 0) kind = tessara::HomeTile::AiTokens;
            else kind = tessara::HomeTile::Generic;
        }
        switch (kind) {
            case tessara::HomeTile::Todoist:
                add_todoist_tile(open_widget_cb);
                break;
            case tessara::HomeTile::Weather:
                add_weather_tile(open_widget_cb);
                break;
            case tessara::HomeTile::AiTokens:
                add_aitokens_tile(open_widget_cb);
                break;
            case tessara::HomeTile::Generic: {
                const char *title = rt.type_title(rt.find_registered(id));
                if (!title || !title[0]) title = id;
                add_generic_tile(title, id);
                break;
            }
            case tessara::HomeTile::None:
                break;
        }
    }
}
}  // namespace

void home::rebuild(const dashboard::Layout &layout) {
    if (!stage || !grid) return;
    if (aitokens_tile) {
        lv_obj_delete(aitokens_tile);
        aitokens_tile = nullptr;
    }
    lv_obj_clean(grid);
    clear_tile_ptrs();
    mount_enabled_tiles(layout);
    arrange();
    apply_theme();
}

void home::begin(lv_event_cb_t on_open_widget, lv_event_cb_t on_settings) {
    open_widget_cb = on_open_widget;
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
    lv_obj_set_size(wordmark, 108, 30);
    lv_image_set_inner_align(wordmark, LV_IMAGE_ALIGN_STRETCH);

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
    kit::tabular_time_set(clock_face_ui, "--:--");
    if (clock_face_ui.root) {
        lv_obj_add_flag(clock_face_ui.root, LV_OBJ_FLAG_CLICKABLE);
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
    if (!clock_timer) clock_timer = lv_timer_create(paint_clock, 1000, nullptr);
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
    if (!task_count) return;
    todoist::Summary summary = {};
    summary.total = total;
    summary.pending = pending;
    char text[16];
    todoist::format_home_count(summary, text, sizeof(text));
    set_if_changed(task_count, text);
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
        snprintf(text, sizeof(text), "%d °C · %s%s", view.forecast.temperature_c, weather::phrase(view.forecast.code),
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
        const aitokens::Source *src = aitokens::find(view, id);
        const bool ok = view.ready && src && src->available && src->display[0];
        if (!ok) {
            if (row) lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        if (row) lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);

        int rem = (src->kind == aitokens::Kind::Pct) ? src->value : 100;
        if (rem < 0) rem = 0;
        if (rem > 100) rem = 100;
        any = true;
        if (rem < worst) worst = rem;

        lv_color_t tone = theme::text_secondary();
        char out[72];
        // Cursor Bridge: "3% · 13%" → home soma "16% usados". Codex: "100% · 88%" → só 5h (+ reset).
        const bool cursor = strcmp(id, "cursor") == 0;
        const bool codex = strcmp(id, "codex") == 0;
        char shown[28];
        if (cursor) {
            int a = -1, b = -1;
            if (sscanf(src->display, "%d%% · %d%%", &a, &b) == 2 && a >= 0 && b >= 0) {
                snprintf(shown, sizeof(shown), "%d%%", a + b);
            } else {
                snprintf(shown, sizeof(shown), "%s", src->display);
            }
        } else if (codex) {
            snprintf(shown, sizeof(shown), "%d%%", rem);
        } else {
            snprintf(shown, sizeof(shown), "%s", src->display);
        }
        const char *unit = cursor ? "usados" : (codex ? "restante" : nullptr);
        if (rem < kAiCriticalPct) {
            tone = theme::error();
            if (src->reset[0] && unit)
                snprintf(out, sizeof(out), "%s · %s %s · alerta · %s", name, shown, unit, src->reset);
            else if (src->reset[0])
                snprintf(out, sizeof(out), "%s · %s · alerta · %s", name, shown, src->reset);
            else if (unit)
                snprintf(out, sizeof(out), "%s · %s %s · alerta", name, shown, unit);
            else
                snprintf(out, sizeof(out), "%s · %s · alerta", name, shown);
        } else if (rem < kAiWarnPct) {
            tone = theme::warning();
            if (src->reset[0] && unit)
                snprintf(out, sizeof(out), "%s · %s %s · baixo · %s", name, shown, unit, src->reset);
            else if (src->reset[0])
                snprintf(out, sizeof(out), "%s · %s · baixo · %s", name, shown, src->reset);
            else if (unit)
                snprintf(out, sizeof(out), "%s · %s %s · baixo", name, shown, unit);
            else
                snprintf(out, sizeof(out), "%s · %s · baixo", name, shown);
        } else if (view.stale) {
            if (unit) snprintf(out, sizeof(out), "%s · %s %s · desatualizado", name, shown, unit);
            else snprintf(out, sizeof(out), "%s · %s · desatualizado", name, shown);
        } else if (src->reset[0]) {
            if (unit) snprintf(out, sizeof(out), "%s · %s %s · %s", name, shown, unit, src->reset);
            else snprintf(out, sizeof(out), "%s · %s · %s", name, shown, src->reset);
        } else if (unit) {
            snprintf(out, sizeof(out), "%s · %s %s", name, shown, unit);
        } else {
            snprintf(out, sizeof(out), "%s · %s", name, shown);
        }
        set_if_changed(label, out);
        if (label) lv_obj_set_style_text_color(label, tone, 0);
    };
    set_row("Codex", "codex", aitokens_line_codex, device.show_aitokens_codex);
    set_row("Claude", "claude", aitokens_line_claude, device.show_aitokens_claude);
    set_row("Cursor", "cursor", aitokens_line_cursor, device.show_aitokens_cursor);

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
        if (any && worst < kAiCriticalPct) {
            border = theme::error();
            width = 2;
        } else if (any && worst < kAiWarnPct) {
            border = theme::warning();
            width = 2;
        }
        lv_obj_set_style_border_color(aitokens_tile, border, 0);
        lv_obj_set_style_border_width(aitokens_tile, width, 0);
    }
    if (aitokens_alert) {
        if (any && worst < kAiWarnPct) {
            lv_obj_set_style_text_color(aitokens_alert, worst < kAiCriticalPct ? theme::error() : theme::warning(), 0);
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
    if (task_icon) lv_obj_set_style_text_color(task_icon, theme::widget_todoist(), 0);
    if (task_count) lv_obj_set_style_text_color(task_count, theme::text_primary(), 0);
    if (weather_icon) lv_obj_set_style_text_color(weather_icon, theme::widget_weather(), 0);
    if (weather_detail) lv_obj_set_style_text_color(weather_detail, theme::text_secondary(), 0);
    if (aitokens_icon) lv_obj_set_style_text_color(aitokens_icon, theme::widget_aitokens(), 0);
    paint_card(tasks_tile);
    paint_card(weather_tile);
    // Só o fundo do card AI Tokens — borda/cores de alerta vêm de set_aitokens.
    if (aitokens_tile) lv_obj_set_style_bg_color(aitokens_tile, theme::surface(), 0);
    auto recolor_name = [](lv_obj_t *card) {
        lv_obj_t *name = card ? lv_obj_get_child(card, 1) : nullptr;
        if (name) lv_obj_set_style_text_color(name, theme::text_primary(), 0);
    };
    recolor_name(tasks_tile);
    recolor_name(weather_tile);
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
