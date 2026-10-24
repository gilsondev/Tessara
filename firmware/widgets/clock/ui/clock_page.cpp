#include "ui/clock_page.h"

#include "clock_summary.h"
#include "ui/kit.h"
#include "ui/palette.h"
#include "ui/theme.h"

#include <cstdio>

namespace {

lv_obj_t *screen = nullptr;
lv_obj_t *title_label = nullptr;
lv_obj_t *time_label = nullptr;
lv_obj_t *date_label = nullptr;
lv_obj_t *sync_label = nullptr;
lv_obj_t *body = nullptr;
lv_event_cb_t back_cb = nullptr;
bool standalone_screen = false;

void build_into(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, theme::background(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_pad_row(parent, 0, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(parent, lv_pct(100), lv_pct(100));

    kit::SubsysBar bar = kit::subsys_bar(parent, LV_SYMBOL_LEFT, back_cb, palette::widget_calendar());
    title_label = bar.title;
    lv_label_set_text(title_label, "Relógio");

    body = kit::bare(parent);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 8, 0);
    lv_obj_set_style_pad_all(body, theme::kListPad, 0);

    time_label = lv_label_create(body);
    lv_label_set_text(time_label, "--:--");
    lv_obj_set_style_text_font(time_label, &theme::display_font(), 0);
    lv_obj_set_style_text_color(time_label, theme::text_primary(), 0);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);

    date_label = lv_label_create(body);
    lv_label_set_text(date_label, "");
    lv_obj_set_style_text_font(date_label, &theme::body_font(), 0);
    lv_obj_set_style_text_color(date_label, theme::text_secondary(), 0);
    lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_CENTER, 0);

    sync_label = lv_label_create(body);
    lv_label_set_text(sync_label, "");
    lv_obj_set_style_text_font(sync_label, &theme::body_font(), 0);
    lv_obj_set_style_text_color(sync_label, theme::muted(), 0);
    lv_obj_set_style_text_align(sync_label, LV_TEXT_ALIGN_CENTER, 0);
}

}  // namespace

void clock_page::begin(lv_event_cb_t on_back) { back_cb = on_back; }

void clock_page::unmount() {
    if (!screen) return;
    if (standalone_screen) lv_obj_delete(screen);
    else lv_obj_clean(screen);
    screen = nullptr;
    title_label = time_label = date_label = sync_label = body = nullptr;
    standalone_screen = false;
}

void clock_page::mount(lv_obj_t *parent) {
    if (!parent) return;
    unmount();
    screen = parent;
    standalone_screen = false;
    build_into(parent);
}

void clock_page::show() {
    if (screen && !standalone_screen) return;
    if (!screen) {
        screen = lv_obj_create(nullptr);
        standalone_screen = true;
        build_into(screen);
    }
    if (standalone_screen) lv_screen_load(screen);
}

void clock_page::apply_theme() {
    if (!screen) return;
    lv_obj_set_style_bg_color(screen, theme::background(), 0);
    if (title_label) lv_obj_set_style_text_color(title_label, theme::text_primary(), 0);
    if (time_label) lv_obj_set_style_text_color(time_label, theme::text_primary(), 0);
    if (date_label) lv_obj_set_style_text_color(date_label, theme::text_secondary(), 0);
    if (sync_label) lv_obj_set_style_text_color(sync_label, theme::muted(), 0);
}

void clock_page::paint(const tessara::TimeSnapshot &snap) {
    if (!time_label) return;
    clock_face::Summary face = clock_face::from_snapshot(snap);
    lv_label_set_text(time_label, face.time);
    if (!face.synced) {
        if (date_label) lv_label_set_text(date_label, "Aguardando sincronização");
        if (sync_label) lv_label_set_text(sync_label, "Horário não sincronizado");
        return;
    }
    if (date_label) lv_label_set_text(date_label, face.date_long);
    if (sync_label) lv_label_set_text(sync_label, "Sincronizado via NTP");
}
