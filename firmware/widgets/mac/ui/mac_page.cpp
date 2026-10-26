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
lv_obj_t *grid = nullptr;
lv_event_cb_t back_cb = nullptr;
mac_page::CmdFn cmd_cb = nullptr;
bool standalone_screen = false;
focus::View cached = {};
bool cached_agent_ok = false;
bool had_good_agent = false;
char pending_id[focus::kIdMax] = {};
bool pending = false;

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

void on_mode(lv_event_t *e) {
    auto *id = static_cast<const char *>(lv_event_get_user_data(e));
    if (pending) return;  // ignore taps while applying
    if (!id || !id[0]) {
        pending = true;
        pending_id[0] = '\0';
        set_status("aplicando…");
        fire_cmd("OFF");
        return;
    }
    pending = true;
    strncpy(pending_id, id, sizeof(pending_id) - 1);
    pending_id[sizeof(pending_id) - 1] = '\0';
    char line[80];
    std::snprintf(line, sizeof(line), "SET\t%s", id);
    set_status("aplicando…");
    fire_cmd(line);
}

lv_obj_t *make_cell(lv_obj_t *parent, const char *label, const char *id, bool active, bool loading) {
    lv_obj_t *cell = kit::bare(parent);
    lv_obj_set_width(cell, 0);
    lv_obj_set_flex_grow(cell, 1);
    lv_obj_set_style_min_width(cell, 0, 0);
    lv_obj_set_height(cell, 72);
    const bool busy = loading;
    lv_obj_set_style_bg_color(cell, active ? lv_color_hex(palette::widget_mac()) : theme::surface(), 0);
    lv_obj_set_style_bg_opa(cell, busy ? LV_OPA_70 : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cell, 14, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, theme::border(), 0);
    lv_obj_set_style_pad_all(cell, 10, 0);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cell, 4, 0);
    if (!busy) {
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        kit::press_feedback(cell);
        lv_obj_add_event_cb(cell, on_mode, LV_EVENT_CLICKED, const_cast<char *>(id));
    }

    lv_obj_t *text = lv_label_create(cell);
    lv_label_set_text(text, label);
    lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
    lv_obj_set_width(text, lv_pct(100));
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(text, &theme::label_font(), 0);
    lv_obj_set_style_text_color(text, active ? lv_color_white() : theme::text_primary(), 0);

    if (busy) {
        kit::spinner(cell, 18, 3);
    } else if (active) {
        lv_obj_t *mark = lv_label_create(cell);
        lv_label_set_text(mark, LV_SYMBOL_OK);
        lv_obj_set_style_text_color(mark, lv_color_white(), 0);
    }
    return cell;
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
    lv_label_set_text(title_label, "Focus");

    body = kit::bare(parent);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(body, 12, 0);
    lv_obj_set_style_pad_top(body, 12, 0);
    lv_obj_set_style_pad_bottom(body, 12, 0);
    lv_obj_set_style_pad_row(body, 8, 0);

    grid = kit::bare(body);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(grid, 8, 0);
    lv_obj_set_style_pad_row(grid, 8, 0);
    kit::enable_list_scroll(grid);
}

void refill_grid(const focus::View &view, bool agent_ok) {
    if (!grid) return;
    lv_obj_clean(grid);

    if (!agent_ok && !had_good_agent) {
        lv_obj_t *msg = lv_label_create(grid);
        lv_label_set_text(msg, "Aguardando Agente…");
        lv_obj_set_style_text_font(msg, &theme::title_font(), 0);
        lv_obj_set_style_text_color(msg, theme::text_primary(), 0);
        set_status("");
        return;
    }

    if (!view.ready || !view.ok) {
        lv_obj_t *msg = lv_label_create(grid);
        lv_label_set_text(msg, view.ready ? "Focus indisponível" : "Aguardando Focus…");
        lv_obj_set_style_text_font(msg, &theme::label_font(), 0);
        lv_obj_set_style_text_color(msg, theme::text_secondary(), 0);
        set_status(view.stale ? "desatualizado" : "");
        return;
    }

    // Clear pending when Mac confirms the requested mode (or none).
    if (pending) {
        const bool want_none = pending_id[0] == '\0';
        const bool got_none = !view.active_id[0];
        if (want_none && got_none) pending = false;
        else if (!want_none && view.active_id[0] && strcmp(view.active_id, pending_id) == 0) pending = false;
    }

    const bool none_active = !view.active_id[0];
    const bool none_loading = pending && pending_id[0] == '\0';
    // Two cells per row: each flex_grow 1 in ROW_WRAP with ~50% via min width trick —
    // set width to (100%-gap)/2 using fixed calc: parent pad 12*2, gap 8 → ~140 on 320.
    auto add = [&](const char *label, const char *id, bool active, bool loading) {
        lv_obj_t *cell = make_cell(grid, label, id, active, loading);
        lv_obj_set_width(cell, 140);
        lv_obj_set_flex_grow(cell, 0);
        (void)cell;
    };
    add("Nenhum", "", none_active, none_loading);
    for (int i = 0; i < view.mode_count; ++i) {
        const bool active = view.active_id[0] && strcmp(view.modes[i].id, view.active_id) == 0;
        const char *label = view.modes[i].name[0] ? view.modes[i].name : view.modes[i].id;
        const bool loading = pending && pending_id[0] && strcmp(view.modes[i].id, pending_id) == 0;
        add(label, view.modes[i].id, active, loading);
    }
    if (pending) set_status("aplicando…");
    else if (view.stale) set_status("desatualizado");
    else if (view.active_name[0]) set_status(view.active_name);
    else set_status("Nenhum");
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
    title_label = status_label = body = grid = nullptr;
    standalone_screen = false;
    pending = false;
    pending_id[0] = '\0';
}

void mac_page::mount(lv_obj_t *parent) {
    if (!parent) return;
    unmount();
    screen = parent;
    standalone_screen = false;
    build_into(parent);
    paint(cached, cached_agent_ok || had_good_agent);
    fire_cmd("?");
}

void mac_page::show() {
    unmount();
    standalone_screen = true;
    screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    build_into(screen);
    paint(cached, cached_agent_ok || had_good_agent);
    fire_cmd("?");
}

void mac_page::apply_theme() {
    if (!screen) return;
    lv_obj_set_style_bg_color(screen, theme::background(), 0);
    if (title_label) lv_obj_set_style_text_color(title_label, theme::text_primary(), 0);
    if (status_label) lv_obj_set_style_text_color(status_label, theme::text_secondary(), 0);
    paint(cached, cached_agent_ok);
}

void mac_page::paint(const focus::View &view, bool agent_ok) {
    if (agent_ok) had_good_agent = true;
    cached = view;
    cached_agent_ok = agent_ok;
    refill_grid(cached, agent_ok);
}

const focus::View &mac_page::last_view() { return cached; }
