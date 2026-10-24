#include "ui/task_page.h"

#include "domain/task_store.h"
#include "ui/kit.h"
#include "ui/theme.h"

namespace {
constexpr int kFirstRows = 7;

constexpr int kMaxRetired = 4;
constexpr int kReapRows = 6;
lv_obj_t *retired[kMaxRetired];
int retired_count = 0;

int32_t closing_height = 0;
int32_t closing_width = 1;

void add_row(lv_obj_t *page, const TaskItem &item, lv_event_cb_t on_mark) {
    lv_obj_t *row = kit::bare(page);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, theme::hairline(), 0);
    lv_obj_set_style_pad_left(row, 4, 0);
    lv_obj_set_style_pad_right(row, 2, 0);
    lv_obj_set_style_pad_ver(row, theme::kRowPad, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_color_t tone = theme::priority(item.priority);
    lv_obj_t *mark = kit::bare(row);
    lv_obj_set_size(mark, theme::kMarkSize, theme::kMarkSize);
    lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(mark, 2, 0);
    lv_obj_set_style_border_color(mark, tone, 0);
    lv_obj_set_style_bg_color(mark, tone, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(mark, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(mark, theme::kMarkReach);
    lv_obj_add_flag(mark, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(mark, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(mark, on_mark, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *column = kit::bare(row);
    lv_obj_set_flex_grow(column, 1);
    lv_obj_set_height(column, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(column, 1, 0);

    const lv_font_t &body = theme::body_font();
    lv_obj_t *title = lv_label_create(column);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &body, 0);
    lv_obj_set_style_text_line_space(title, 1, 0);
    lv_obj_set_style_max_height(title, body.line_height * 2 + 1, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(title, theme::ink(), 0);
    lv_label_set_text(title, item.content);
    if (item.meta[0]) {
        lv_obj_t *meta = lv_label_create(column);
        lv_obj_set_width(meta, lv_pct(100));
        lv_obj_set_style_text_font(meta, &theme::small_font(), 0);
        lv_obj_set_style_text_color(meta, theme::muted(), 0);
        lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
        lv_label_set_text(meta, item.meta);
    }
}

void fill_message(lv_obj_t *page, const char *text) {
    lv_obj_t *label = lv_label_create(page);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(label, 4, 0);
    lv_obj_set_style_pad_top(label, 8, 0);
    lv_obj_set_style_text_font(label, &theme::body_font(), 0);
    lv_obj_set_style_text_color(label, theme::ink(), 0);
    lv_label_set_text(label, text ? text : "");
}

void fill_loading(lv_obj_t *page) {
    lv_obj_t *box = kit::bare(page);
    lv_obj_set_width(box, lv_pct(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(box, 36, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 14, 0);
    kit::spinner(box, 48, 8);
}

void row_anim(void *obj, int32_t value) {
    lv_obj_t *row = static_cast<lv_obj_t *>(obj);
    lv_obj_set_style_translate_x(row, value, 0);
    int32_t traveled = value < 0 ? -value : value;
    if (traveled > closing_width) traveled = closing_width;
    lv_obj_set_style_opa(row, static_cast<lv_opa_t>(LV_OPA_COVER - (traveled * (LV_OPA_COVER - 40)) / closing_width), 0);
    if (closing_height <= 0) return;
    int32_t height = closing_height - (closing_height * traveled) / closing_width;
    lv_obj_set_height(row, height > 0 ? height : 0);
    int32_t pad = theme::kRowPad - (theme::kRowPad * traveled) / closing_width;
    lv_obj_set_style_pad_ver(row, pad > 0 ? pad : 0, 0);
    if (traveled * 3 > closing_width) lv_obj_set_style_border_width(row, 0, 0);
}
}  // namespace

int32_t task_page::width_for(int32_t screen_width) {
    int32_t width = screen_width - theme::kListPad * 2;
    return width < 120 ? 120 : width;
}

void task_page::create(Page &page, lv_obj_t *parent, int32_t width) {
    page = Page{};
    page.obj = kit::bare(parent);
    lv_obj_set_width(page.obj, width);
    lv_obj_set_height(page.obj, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(page.obj, theme::ink(), 0);
    lv_obj_set_style_min_height(page.obj, 120, 0);
    lv_obj_set_flex_flow(page.obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(page.obj, LV_OBJ_FLAG_EVENT_BUBBLE);
}

void task_page::destroy(Page &page) {
    if (page.obj) lv_obj_delete(page.obj);
    page = Page{};
}

void task_page::retire(Page &page) {
    if (!page.obj) return;
    if (retired_count < kMaxRetired) {
        lv_obj_add_flag(page.obj, LV_OBJ_FLAG_HIDDEN);
        retired[retired_count++] = page.obj;
    } else {
        lv_obj_delete(page.obj);
    }
    page = Page{};
}

bool task_page::reap() {
    if (!retired_count) return false;
    lv_obj_t *obj = retired[0];
    for (int i = 0; i < kReapRows; ++i) {
        uint32_t left = lv_obj_get_child_count(obj);
        if (!left) break;
        lv_obj_delete(lv_obj_get_child(obj, left - 1));
    }
    if (!lv_obj_get_child_count(obj)) {
        lv_obj_delete(obj);
        --retired_count;
        for (int i = 0; i < retired_count; ++i) retired[i] = retired[i + 1];
    }
    return true;
}

bool task_page::paint(Page &page, int view, lv_event_cb_t on_mark) {
    if (!page.obj) return false;
    lv_obj_clean(page.obj);
    page.view = view;
    page.version = task_store::version(view);
    page.count = 0;
    page.built = 0;
    switch (task_store::state(view)) {
        case task_store::State::Empty:
            fill_loading(page.obj);
            return false;
        case task_store::State::Failed:
            fill_message(page.obj, task_store::error(view));
            return false;
        case task_store::State::Ready:
            break;
    }
    task_store::tasks(view, &page.count);
    if (page.count == 0) {
        fill_message(page.obj, "Nada por aqui.");
        return false;
    }
    return grow(page, kFirstRows, on_mark) > 0;
}

int task_page::grow(Page &page, int rows, lv_event_cb_t on_mark) {
    if (!page.obj || page.built >= page.count) return 0;
    int count = 0;
    const TaskItem *items = task_store::tasks(page.view, &count);
    if (count < page.count) page.count = count;
    int end = page.built + rows;
    if (end > page.count) end = page.count;
    for (int i = page.built; i < end; ++i) add_row(page.obj, items[i], on_mark);
    int added = end - page.built;
    page.built = end;
    return added;
}

bool task_page::growing(const Page &page) { return page.obj && page.built < page.count; }

int task_page::row_of(const Page &page, lv_obj_t *mark) {
    lv_obj_t *row = mark ? lv_obj_get_parent(mark) : nullptr;
    if (!row || lv_obj_get_parent(row) != page.obj) return -1;
    return static_cast<int>(lv_obj_get_index(row));
}

void task_page::remove_row(Page &page, int row) {
    if (!page.obj) return;
    lv_obj_t *child = lv_obj_get_child(page.obj, row);
    if (child && row < page.built) {
        lv_obj_delete(child);
        page.built--;
    }
    page.count--;
    page.version = task_store::version(page.view);
    if (page.count <= 0) {
        lv_obj_clean(page.obj);
        page.count = page.built = 0;
        fill_message(page.obj, "Nada por aqui.");
    }
}

void task_page::animate_completion(lv_obj_t *mark, lv_anim_completed_cb_t done, void *user_data) {
    lv_obj_set_style_bg_color(mark, theme::red(), 0);
    lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(mark, theme::red(), 0);
    lv_obj_remove_flag(mark, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *row = lv_obj_get_parent(mark);
    closing_height = lv_obj_get_height(row);
    closing_width = lv_obj_get_width(row);
    if (closing_width < 1) closing_width = 1;
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, row);
    lv_anim_set_exec_cb(&anim, row_anim);
    lv_anim_set_values(&anim, 0, -closing_width);
    lv_anim_set_duration(&anim, 240);
    lv_anim_set_delay(&anim, 60);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_user_data(&anim, user_data);
    lv_anim_set_completed_cb(&anim, done);
    lv_anim_start(&anim);
}
