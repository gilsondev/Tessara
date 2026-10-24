#include "ui/detail_host.h"

#include "ui/kit.h"
#include "ui/theme.h"

namespace {

lv_obj_t *root = nullptr;
lv_obj_t *content = nullptr;
bool shown = false;

void ensure() {
    if (root) return;
    root = kit::bare(lv_layer_top());
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(root, theme::background(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    content = kit::bare(root);
    lv_obj_set_size(content, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
}

}  // namespace

void detail_host::begin() { ensure(); }

lv_obj_t *detail_host::open() {
    ensure();
    lv_obj_clean(content);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root);
    shown = true;
    return content;
}

void detail_host::close() {
    if (!root) return;
    lv_obj_clean(content);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    shown = false;
}

bool detail_host::visible() { return shown; }

lv_obj_t *detail_host::slot() {
    ensure();
    return content;
}
