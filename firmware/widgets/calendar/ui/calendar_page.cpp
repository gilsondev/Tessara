#include "ui/calendar_page.h"

#include "ui/calendar_icons.h"
#include "ui/calendar_icons_data.h"
#include "ui/kit.h"
#include "ui/palette.h"
#include "ui/theme.h"

#include <cstdio>
#include <cstring>

namespace {

lv_obj_t *screen = nullptr;
lv_obj_t *title_label = nullptr;
lv_obj_t *status_label = nullptr;
lv_obj_t *list = nullptr;
lv_event_cb_t back_cb = nullptr;
bool standalone_screen = false;
bool preserve_scroll_ = false;
calendar::View cached = {};

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

void format_time(char *out, size_t n, int minutes) {
    if (!out || n == 0) return;
    int h = minutes / 60;
    int m = minutes % 60;
    snprintf(out, n, "%02d:%02d", h, m);
}

void format_duration(char *out, size_t n, int minutes) {
    if (!out || n == 0) return;
    if (minutes <= 0) {
        out[0] = '\0';
        return;
    }
    if (minutes < 60) {
        snprintf(out, n, "%d min", minutes);
        return;
    }
    int h = minutes / 60;
    int m = minutes % 60;
    if (m == 0) snprintf(out, n, "%d h", h);
    else snprintf(out, n, "%d h %d min", h, m);
}

void day_title(char *out, size_t n, int day_offset, int count) {
    if (!out || n == 0) return;
    const char *head = "Hoje";
    char head_buf[24];
    if (day_offset == 1) {
        head = "Amanhã";
    } else if (day_offset > 1) {
        snprintf(head_buf, sizeof(head_buf), "Em %d dias", day_offset);
        head = head_buf;
    }
    if (count > 0) snprintf(out, n, "%s · %d", head, count);
    else snprintf(out, n, "%s", head);
}

void enable_list_scroll(lv_obj_t *obj) {
    if (!obj) return;
    // kit::bare strips CLICKABLE — without it LVGL never feeds the drag to the scroller.
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(obj, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_scroll_snap_y(obj, LV_SCROLL_SNAP_NONE);
    lv_obj_set_style_bg_color(obj, theme::muted(), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(obj, LV_OPA_40, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(obj, 3, LV_PART_SCROLLBAR);
}

void add_centered_state(const lv_image_dsc_t *icon, const char *title, const char *hint, lv_color_t tone) {
    lv_obj_t *box = kit::bare(list);
    lv_obj_set_width(box, lv_pct(100));
    lv_obj_set_flex_grow(box, 1);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 10, 0);
    lv_obj_set_style_pad_hor(box, 16, 0);

    lv_obj_t *slot = kit::bare(box);
    lv_obj_set_size(slot, 52, 52);
    lv_obj_t *img = lv_image_create(slot);
    lv_image_set_src(img, icon);
    lv_image_set_scale(img, (48 * 256) / calendar_icons_data::side());
    lv_obj_set_style_image_recolor(img, tone, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);

    lv_obj_t *label = lv_label_create(box);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &theme::body_font(), 0);
    lv_obj_set_style_text_color(label, theme::text_primary(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    if (hint && hint[0]) {
        lv_obj_t *sub = lv_label_create(box);
        lv_label_set_text(sub, hint);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(sub, lv_pct(100));
        lv_obj_set_style_text_font(sub, &theme::small_font(), 0);
        lv_obj_set_style_text_color(sub, theme::text_secondary(), 0);
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void add_summary() {
    // Next-up card — one glance at what matters first.
    const calendar::Event *next = nullptr;
    for (int i = 0; i < cached.event_count; ++i) {
        if (cached.events[i].day_offset == 0) {
            next = &cached.events[i];
            break;
        }
    }
    if (!next && cached.event_count > 0) next = &cached.events[0];

    lv_obj_t *card = kit::bare(list);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, theme::surface(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, theme::border(), 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_style_margin_bottom(card, 10, 0);

    lv_obj_t *eyebrow = lv_label_create(card);
    lv_label_set_text(eyebrow, next && next->day_offset == 0 ? "Próximo hoje" : "Próximo");
    lv_obj_set_style_text_font(eyebrow, &theme::small_font(), 0);
    lv_obj_set_style_text_color(eyebrow, theme::widget_calendar(), 0);

    if (!next) return;

    char when[40];
    if (next->all_day) {
        snprintf(when, sizeof(when), "Dia inteiro");
    } else {
        char a[8], b[8];
        format_time(a, sizeof(a), next->start_min);
        format_time(b, sizeof(b), next->end_min);
        if (next->end_min > next->start_min) snprintf(when, sizeof(when), "%s–%s", a, b);
        else snprintf(when, sizeof(when), "%s", a);
    }

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, next->title[0] ? next->title : "Evento");
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &theme::body_font(), 0);
    lv_obj_set_style_text_color(title, theme::text_primary(), 0);

    char meta[96];
    if (next->day_offset == 0) {
        snprintf(meta, sizeof(meta), "%s", when);
    } else if (next->day_offset == 1) {
        snprintf(meta, sizeof(meta), "Amanhã · %s", when);
    } else {
        snprintf(meta, sizeof(meta), "Em %d dias · %s", next->day_offset, when);
    }
    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, meta);
    lv_obj_set_style_text_font(sub, &theme::small_font(), 0);
    lv_obj_set_style_text_color(sub, theme::text_secondary(), 0);
}

void add_event_into(lv_obj_t *parent, const calendar::Event &event, bool last) {
    lv_obj_t *row = kit::bare(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_ver(row, 10, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    if (!last) {
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, theme::border(), 0);
    }

    lv_obj_t *bar = kit::bare(row);
    lv_obj_set_size(bar, 4, 48);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(event.color_rgb ? event.color_rgb : 0x1A73E8u), 0);

    lv_obj_t *body = kit::bare(row);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_width(body, 0);
    lv_obj_set_height(body, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 3, 0);

    // Time line first — scan pattern: when, then what.
    char when[40];
    if (event.all_day) {
        snprintf(when, sizeof(when), "Dia inteiro");
    } else {
        char a[8], b[8];
        format_time(a, sizeof(a), event.start_min);
        format_time(b, sizeof(b), event.end_min);
        if (event.end_min > event.start_min) snprintf(when, sizeof(when), "%s – %s", a, b);
        else snprintf(when, sizeof(when), "%s", a);
    }
    lv_obj_t *when_lbl = lv_label_create(body);
    lv_label_set_text(when_lbl, when);
    lv_obj_set_style_text_font(when_lbl, &theme::small_font(), 0);
    lv_obj_set_style_text_color(when_lbl, theme::widget_calendar(), 0);

    lv_obj_t *title = lv_label_create(body);
    lv_label_set_text(title, event.title[0] ? event.title : "Evento");
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &theme::body_font(), 0);
    lv_obj_set_style_text_color(title, theme::text_primary(), 0);

    char meta[96];
    char dur[24];
    format_duration(dur, sizeof(dur), event.duration_min);
    if (event.location[0] && dur[0] && !event.all_day) {
        snprintf(meta, sizeof(meta), "%s · %s", event.location, dur);
    } else if (event.location[0]) {
        snprintf(meta, sizeof(meta), "%s", event.location);
    } else if (dur[0] && !event.all_day) {
        snprintf(meta, sizeof(meta), "%s", dur);
    } else {
        meta[0] = '\0';
    }
    if (meta[0]) {
        lv_obj_t *sub = lv_label_create(body);
        lv_label_set_text(sub, meta);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
        lv_obj_set_width(sub, lv_pct(100));
        lv_obj_set_style_text_font(sub, &theme::small_font(), 0);
        lv_obj_set_style_text_color(sub, theme::text_secondary(), 0);
    }
}

void add_day_card(int day_offset, int begin, int end) {
    const int count = end - begin;
    lv_obj_t *card = kit::bare(list);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, theme::surface(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, theme::border(), 0);
    lv_obj_set_style_pad_hor(card, 12, 0);
    lv_obj_set_style_pad_top(card, 10, 0);
    lv_obj_set_style_pad_bottom(card, 2, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 0, 0);
    lv_obj_set_style_margin_bottom(card, 10, 0);

    char head[40];
    day_title(head, sizeof(head), day_offset, count);
    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, head);
    lv_obj_set_style_text_font(label, &theme::label_font(), 0);
    lv_obj_set_style_text_color(label, theme::text_primary(), 0);
    lv_obj_set_style_pad_bottom(label, 4, 0);

    for (int i = begin; i < end; ++i) {
        add_event_into(card, cached.events[i], i + 1 == end);
    }
}

void rebuild_body() {
    if (!list) return;
    const int32_t scroll_y = preserve_scroll_ ? lv_obj_get_scroll_y(list) : 0;
    lv_obj_clean(list);

    if (!cached.bridge_ok && !cached.ready) {
        add_centered_state(calendar_icons::warn(), "Sem conexão", "Confira o cabo USB e o Agente no Mac.",
                           theme::error());
        return;
    }
    if (!cached.linked) {
        add_centered_state(calendar_icons::month(), "Agenda desligada",
                           "No portal, abra Agenda e marque os calendários.", theme::widget_calendar());
        return;
    }
    if (!cached.ready) {
        lv_obj_t *box = kit::bare(list);
        lv_obj_set_width(box, lv_pct(100));
        lv_obj_set_flex_grow(box, 1);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *spin = kit::spinner(box, 36, 4);
        lv_obj_t *label = lv_label_create(box);
        lv_label_set_text(label, "Carregando agenda…");
        lv_obj_set_style_text_font(label, &theme::body_font(), 0);
        lv_obj_set_style_text_color(label, theme::text_secondary(), 0);
        lv_obj_set_style_pad_top(label, 10, 0);
        (void)spin;
        return;
    }
    if (cached.event_count <= 0) {
        add_centered_state(calendar_icons::busy(), "Agenda livre", "Nada nos próximos 7 dias.", theme::muted());
        return;
    }

    add_summary();

    int i = 0;
    while (i < cached.event_count) {
        const int day = cached.events[i].day_offset;
        int j = i + 1;
        while (j < cached.event_count && cached.events[j].day_offset == day) ++j;
        add_day_card(day, i, j);
        i = j;
    }

    lv_obj_t *spacer = kit::bare(list);
    lv_obj_set_width(spacer, lv_pct(100));
    lv_obj_set_height(spacer, 20);

    if (preserve_scroll_) lv_obj_scroll_to_y(list, scroll_y, LV_ANIM_OFF);
    else lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
}

void update_status_line() {
    if (!cached.ready) {
        set_status("");
        return;
    }
    if (cached.stale) {
        set_status("desatualizado");
        return;
    }
    if (!cached.bridge_ok) {
        set_status("sem conexão");
        return;
    }
    if (cached.event_count <= 0) {
        set_status("livre");
        return;
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "%d evento%s", cached.event_count, cached.event_count == 1 ? "" : "s");
    set_status(buf);
}

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
    status_label = bar.status;
    lv_label_set_text(title_label, "Calendário");

    list = kit::bare(parent);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_height(list, 0);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_hor(list, theme::kListPad, 0);
    lv_obj_set_style_pad_top(list, 6, 0);
    lv_obj_set_style_pad_bottom(list, theme::kListPad + 8, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 0, 0);
    enable_list_scroll(list);
}

}  // namespace

void calendar_page::begin(lv_event_cb_t on_back) { back_cb = on_back; }

void calendar_page::unmount() {
    if (!screen) return;
    if (standalone_screen) lv_obj_delete(screen);
    else lv_obj_clean(screen);
    screen = nullptr;
    title_label = nullptr;
    status_label = nullptr;
    list = nullptr;
    standalone_screen = false;
    preserve_scroll_ = false;
}

void calendar_page::mount(lv_obj_t *parent) {
    if (!parent) return;
    unmount();
    screen = parent;
    standalone_screen = false;
    preserve_scroll_ = false;
    build_into(parent);
    paint(cached);
}

void calendar_page::show() {
    if (screen && !standalone_screen) return;
    if (!screen) {
        screen = lv_obj_create(nullptr);
        standalone_screen = true;
        preserve_scroll_ = false;
        build_into(screen);
    }
    if (standalone_screen) lv_screen_load(screen);
    paint(cached);
}

void calendar_page::apply_theme() {
    if (!screen) return;
    lv_obj_set_style_bg_color(screen, theme::background(), 0);
    if (title_label) lv_obj_set_style_text_color(title_label, theme::text_primary(), 0);
    if (status_label) lv_obj_set_style_text_color(status_label, theme::muted(), 0);
    if (list) {
        lv_obj_set_style_bg_color(list, theme::muted(), LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(list, LV_OPA_40, LV_PART_SCROLLBAR);
    }
    preserve_scroll_ = true;
    rebuild_body();
}

void calendar_page::paint(const calendar::View &view) {
    const bool had_list = screen && list;
    cached = view;
    if (!screen || !list) return;
    update_status_line();
    // Keep scroll position across live USB refreshes; reset only on first paint after mount.
    preserve_scroll_ = had_list && preserve_scroll_;
    rebuild_body();
    preserve_scroll_ = true;
}

const calendar::View &calendar_page::last_view() { return cached; }
