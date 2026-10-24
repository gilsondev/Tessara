#include "ui/weather_page.h"

#include "ui/kit.h"
#include "ui/palette.h"
#include "ui/theme.h"
#include "ui/weather_icons.h"
#include "weather/codes.h"

#include <cstdio>
#include <cstring>

namespace {
lv_obj_t *screen = nullptr;
lv_obj_t *title_label = nullptr;
lv_obj_t *status_label = nullptr;
lv_obj_t *list = nullptr;
lv_event_cb_t back_cb = nullptr;
bool standalone_screen = false;

int weekday_index(const char *iso) {
    if (!iso || strlen(iso) < 10) return -1;
    int y = 0, m = 0, d = 0;
    if (sscanf(iso, "%d-%d-%d", &y, &m, &d) != 3) return -1;
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

const char *weekday_short(const char *iso) {
    static const char *names[] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SÁB"};
    int w = weekday_index(iso);
    if (w < 0) return "—";
    return names[w];
}

void set_status(const char *text) {
    if (!status_label) return;
    if (!text || !text[0]) {
        lv_label_set_text(status_label, "");
        lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(status_label, text);
    lv_obj_remove_flag(status_label, LV_OBJ_FLAG_HIDDEN);
}

void add_today_hero(const weather::View &view) {
    const weather::Day &day = view.forecast.days[0];

    lv_obj_t *hero = kit::bare(list);
    lv_obj_set_width(hero, lv_pct(100));
    lv_obj_set_height(hero, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(hero, 6, 0);
    lv_obj_set_style_pad_row(hero, 2, 0);

    lv_obj_t *when = lv_label_create(hero);
    lv_label_set_text(when, "Hoje");
    lv_obj_set_style_text_font(when, &theme::body_font(), 0);
    lv_obj_set_style_text_color(when, theme::text_primary(), 0);
    lv_obj_set_style_text_align(when, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *slot = kit::bare(hero);
    lv_obj_set_size(slot, 88, 88);
    lv_obj_t *icon = lv_image_create(slot);
    lv_image_set_src(icon, weather_icons::for_code(view.forecast.code));
    lv_image_set_scale(icon, 236);
    lv_obj_center(icon);

    lv_obj_t *temp = lv_label_create(hero);
    char temp_text[16];
    snprintf(temp_text, sizeof(temp_text), "%d°", view.forecast.temperature_c);
    lv_label_set_text(temp, temp_text);
    lv_obj_set_style_text_font(temp, &theme::display_font(), 0);
    lv_obj_set_style_text_color(temp, theme::text_primary(), 0);
    lv_obj_set_style_text_align(temp, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *condition = lv_label_create(hero);
    lv_label_set_text(condition, weather::phrase(view.forecast.code));
    lv_obj_set_style_text_font(condition, &theme::body_font(), 0);
    lv_obj_set_style_text_color(condition, theme::text_secondary(), 0);
    lv_obj_set_style_text_align(condition, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *range = lv_label_create(hero);
    char range_text[32];
    snprintf(range_text, sizeof(range_text), "máx %d°  ·  mín %d°", day.temperature_max_c, day.temperature_min_c);
    lv_label_set_text(range, range_text);
    lv_obj_set_style_text_font(range, &theme::small_font(), 0);
    lv_obj_set_style_text_color(range, theme::muted(), 0);
    lv_obj_set_style_text_align(range, LV_TEXT_ALIGN_CENTER, 0);
}

void add_day_cell(lv_obj_t *grid, int index, const weather::Day &day) {
    lv_obj_t *cell = kit::bare(grid);
    lv_obj_set_width(cell, lv_pct(31));
    lv_obj_set_height(cell, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(cell, 8, 0);
    lv_obj_set_style_pad_hor(cell, 4, 0);
    lv_obj_set_style_pad_row(cell, 3, 0);
    lv_obj_set_style_bg_color(cell, theme::surface(), 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cell, 10, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, theme::border(), 0);

    lv_obj_t *when = lv_label_create(cell);
    char label[16];
    if (index == 1) snprintf(label, sizeof(label), "Amanhã");
    else snprintf(label, sizeof(label), "%s", weekday_short(day.date));
    lv_label_set_text(when, label);
    lv_obj_set_style_text_font(when, &theme::small_font(), 0);
    lv_obj_set_style_text_color(when, theme::text_primary(), 0);
    lv_obj_set_style_text_align(when, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *slot = kit::bare(cell);
    lv_obj_set_size(slot, 40, 40);
    lv_obj_t *icon = lv_image_create(slot);
    lv_image_set_src(icon, weather_icons::for_code(day.code));
    lv_image_set_scale(icon, 128);
    lv_obj_center(icon);

    lv_obj_t *temps = lv_label_create(cell);
    char text[20];
    snprintf(text, sizeof(text), "%d°/%d°", day.temperature_max_c, day.temperature_min_c);
    lv_label_set_text(temps, text);
    lv_obj_set_style_text_font(temps, &theme::small_font(), 0);
    lv_obj_set_style_text_color(temps, theme::text_secondary(), 0);
    lv_obj_set_style_text_align(temps, LV_TEXT_ALIGN_CENTER, 0);
}

void add_forecast_grid(const weather::View &view) {
    lv_obj_t *grid = kit::bare(list);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_top(grid, 8, 0);
    lv_obj_set_style_pad_row(grid, 6, 0);
    lv_obj_set_style_pad_bottom(grid, 4, 0);
    lv_obj_set_style_pad_column(grid, 0, 0);

    for (int i = 1; i < 7; ++i) add_day_cell(grid, i, view.forecast.days[i]);
}

void build_into(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, theme::background(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_pad_row(parent, 0, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(parent, lv_pct(100), lv_pct(100));

    kit::SubsysBar bar = kit::subsys_bar(parent, LV_SYMBOL_LEFT, back_cb, palette::widget_weather());
    title_label = bar.title;
    status_label = bar.status;
    lv_label_set_text(title_label, "Clima");

    list = kit::bare(parent);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_hor(list, theme::kListPad, 0);
    lv_obj_set_style_pad_bottom(list, theme::kListPad, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(list, theme::muted(), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_40, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);
}

}  // namespace

void weather_page::begin(lv_event_cb_t on_back) { back_cb = on_back; }

void weather_page::unmount() {
    if (!screen) return;
    if (standalone_screen) {
        // Standalone screens are not children of detail_host; delete the screen object.
        lv_obj_delete(screen);
    } else {
        lv_obj_clean(screen);
    }
    screen = nullptr;
    title_label = nullptr;
    status_label = nullptr;
    list = nullptr;
    standalone_screen = false;
}

void weather_page::mount(lv_obj_t *parent) {
    if (!parent) return;
    unmount();
    screen = parent;
    standalone_screen = false;
    build_into(parent);
}

void weather_page::show() {
    if (screen && !standalone_screen) return;  // already in detail slot
    if (!screen) {
        screen = lv_obj_create(nullptr);
        standalone_screen = true;
        build_into(screen);
    }
    if (standalone_screen) lv_screen_load(screen);
}

void weather_page::apply_theme() {
    if (!screen) return;
    lv_obj_set_style_bg_color(screen, theme::background(), 0);
    if (title_label) lv_obj_set_style_text_color(title_label, theme::text_primary(), 0);
    if (status_label) lv_obj_set_style_text_color(status_label, theme::muted(), 0);
}

void weather_page::paint(const weather::View &view) {
    if (!screen || !list) return;
    lv_obj_clean(list);
    if (!view.placed) {
        lv_label_set_text(title_label, "Clima");
        set_status("definir local");
        return;
    }
    lv_label_set_text(title_label, view.name[0] ? view.name : "Clima");
    if (view.failed && !view.ready) {
        set_status("falhou");
        return;
    }
    if (!view.ready) {
        set_status("carregando…");
        return;
    }
    set_status(view.stale ? "desatualizado" : "");
    add_today_hero(view);
    add_forecast_grid(view);
}
