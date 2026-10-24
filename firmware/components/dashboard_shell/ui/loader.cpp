#include "ui/loader.h"

#include "ui/kit.h"
#include "ui/theme.h"

#include <cstring>

namespace {
lv_obj_t *root = nullptr;
lv_obj_t *title_label = nullptr;
lv_obj_t *detail_label = nullptr;
lv_obj_t *track = nullptr;
lv_obj_t *fill = nullptr;
char title_text[48] = "";
char detail_text[64] = "";
int progress = -1;

void apply_progress() {
    if (!track) return;
    if (progress < 0) {
        lv_obj_add_flag(track, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(track, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(fill, lv_pct(progress > 100 ? 100 : progress));
}

void build() {
    root = kit::bare(lv_layer_top());
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(root, theme::paper(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, 10, 0);

    lv_obj_t *stripe = kit::bare(root);
    lv_obj_add_flag(stripe, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(stripe, lv_pct(100), 4);
    lv_obj_align(stripe, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(stripe, theme::primary(), 0);
    lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);

    kit::spinner(root, 72, 12);

    title_label = lv_label_create(root);
    lv_obj_set_style_text_font(title_label, &theme::title_font(), 0);
    lv_obj_set_style_text_color(title_label, theme::ink(), 0);
    lv_obj_set_style_pad_top(title_label, 8, 0);
    lv_label_set_text(title_label, title_text);

    detail_label = lv_label_create(root);
    lv_obj_set_style_text_font(detail_label, &theme::label_font(), 0);
    lv_obj_set_style_text_color(detail_label, theme::muted(), 0);
    lv_label_set_text(detail_label, detail_text);

    track = kit::bare(root);
    lv_obj_set_size(track, 180, 4);
    lv_obj_set_style_radius(track, 2, 0);
    lv_obj_set_style_bg_color(track, theme::wash(), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_margin_top(track, 6, 0);
    fill = kit::bare(track);
    lv_obj_set_height(fill, lv_pct(100));
    lv_obj_set_style_radius(fill, 2, 0);
    lv_obj_set_style_bg_color(fill, theme::primary(), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    apply_progress();
}

void set_label(lv_obj_t *label, char *store, size_t cap, const char *value) {
    if (!value) value = "";
    if (!strcmp(store, value)) return;
    strncpy(store, value, cap - 1);
    store[cap - 1] = '\0';
    if (label) {
        lv_label_set_text(label, store);
        if (store[0]) lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}
}  // namespace

void loader::show(const char *title, const char *detail) {
    set_label(title_label, title_text, sizeof(title_text), title);
    set_label(detail_label, detail_text, sizeof(detail_text), detail);
    if (!root) build();
}

void loader::set_progress(int percent) {
    if (percent == progress) return;
    progress = percent;
    apply_progress();
}

void loader::hide() {
    if (!root) return;
    lv_obj_delete(root);
    root = title_label = detail_label = track = fill = nullptr;
    progress = -1;
}

bool loader::visible() { return root != nullptr; }

void loader::relayout() {
    if (!root) return;
    lv_obj_delete(root);
    root = nullptr;
    build();
}
