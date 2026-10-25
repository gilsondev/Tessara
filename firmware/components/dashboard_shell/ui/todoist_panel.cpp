#include "ui/todoist_panel.h"

#include "domain/model.h"
#include "domain/task_store.h"
#include "ui/bitmaps.h"
#include "ui/kit.h"
#include "ui/palette.h"
#include "ui/task_page.h"
#include "ui/theme.h"

#include <esp_timer.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {

constexpr int kGrowRows = 4;
// Must stay below the pointer's scroll limit (input_host): the swipe direction is decided before LVGL
// starts a vertical scroll, otherwise a diagonal swipe drives both and the page sticks.
constexpr int kDragLock = 6;
constexpr int kStripStart = 4;  // px of page drag before the neighbour is built
// Background renders hold the UI thread; run them only after the user has paused, so a burst of
// checks or swipes never waits on one.
constexpr uint32_t kCacheQuietMs = 400;
constexpr uint32_t kPreviewQuietMs = 1200;

todoist_panel::Hooks hooks = {};
lv_obj_t *root = nullptr;
lv_obj_t *standalone = nullptr;  // sim/legacy lv_screen_load host (root itself is never a screen)
lv_obj_t *header = nullptr;
lv_obj_t *title_label = nullptr;
lv_obj_t *status_label = nullptr;
lv_obj_t *tabs = nullptr;
lv_obj_t *list = nullptr;
lv_obj_t *sheet = nullptr;
task_page::Page current;
task_page::Page other;
int active = 0;
int pending_view = -1;
bool sliding = false;
int completing = 0;
constexpr int kMaxDone = 8;
lv_obj_t *done_rows[kMaxDone];
int done_count = 0;
uint32_t epoch = 1;  // bumps whenever the current page's pixels may change

struct Gesture {
    bool tracking;
    bool page_drag;
    bool suppress_click;
    int x;
    int y;
    int strip_dir;
    int32_t strip_rest;
} gesture = {};

todoist_panel::Stats stats = {};

struct Timed {
    uint32_t &slot;
    int64_t start = esp_timer_get_time();
    explicit Timed(uint32_t &target) : slot(target) {}
    ~Timed() {
        uint32_t spent = static_cast<uint32_t>(esp_timer_get_time() - start);
        if (spent > slot) slot = spent;
    }
};

void on_mark(lv_event_t *event);

void touch() { ++epoch; }

int32_t page_width() { return task_page::width_for(lv_display_get_horizontal_resolution(nullptr)); }

int gap(int value) { return value < 0 ? -value : value; }

void set_shift(int32_t x) {
    if (bitmaps::frozen()) bitmaps::set_frozen_shift(x);
    else if (sheet) lv_obj_set_style_translate_x(sheet, x, 0);
}

int32_t shift() {
    if (bitmaps::frozen()) return bitmaps::frozen_shift();
    return sheet ? lv_obj_get_style_translate_x(sheet, 0) : 0;
}

void place_sheet(int32_t x) { lv_obj_set_style_translate_x(sheet, x, 0); }

void thaw() {
    if (!bitmaps::frozen()) return;
    int32_t x = bitmaps::frozen_shift();
    bitmaps::unfreeze();
    set_shift(x);
}

void shift_anim(void *, int32_t value) { set_shift(value); }

void paint_tab_counts() {
    if (!tabs) return;
    int views = task_store::view_count();
    uint32_t chips = lv_obj_get_child_count(tabs);
    for (uint32_t i = 0; i < chips; ++i) {
        int count = -1;
        if (static_cast<int>(i) < views && task_store::state(static_cast<int>(i)) == task_store::State::Ready) {
            task_store::tasks(static_cast<int>(i), &count);
        }
        kit::set_chip_count(lv_obj_get_child(tabs, i), count);
    }
}

void announce() {
    paint_tab_counts();
    if (!status_label) return;
    char text[32] = "";
    int view = current.view;
    switch (task_store::state(view)) {
        case task_store::State::Ready: break;
        case task_store::State::Empty: snprintf(text, sizeof(text), "carregando"); break;
        case task_store::State::Failed: snprintf(text, sizeof(text), "falhou"); break;
    }
    if (text[0]) {
        if (strcmp(lv_label_get_text(status_label), text) != 0) lv_label_set_text(status_label, text);
        lv_obj_remove_flag(status_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(status_label, "");
        lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void select_tab(int view, bool scroll_anim) {
    if (!tabs) return;
    uint32_t count = lv_obj_get_child_count(tabs);
    for (uint32_t i = 0; i < count; ++i) {
        // No bold swap — font change resizes chips and makes the tab strip jump.
        kit::set_chip_selected(lv_obj_get_child(tabs, i), static_cast<int>(i) == view, false);
    }
    lv_obj_t *chip = lv_obj_get_child(tabs, view);
    if (chip) lv_obj_scroll_to_view(chip, scroll_anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

void deliver_done(void *);

// Rows whose exit animation is still running would be lost with the page; finish them first.
void settle_completions() {
    deliver_done(nullptr);
    if (!completing || !current.obj) return;
    for (int i = static_cast<int>(lv_obj_get_child_count(current.obj)) - 1; i >= 0; --i) {
        lv_obj_t *row = lv_obj_get_child(current.obj, i);
        if (!lv_anim_get(row, nullptr)) continue;
        lv_anim_delete(row, nullptr);
        if (hooks.on_task_completed) hooks.on_task_completed(current.view, i);
    }
    completing = 0;
}

void drop_other() {
    thaw();
    task_page::destroy(other);
    if (!sheet) return;
    lv_obj_set_width(sheet, page_width());
    gesture.strip_dir = 0;
    gesture.strip_rest = 0;
    set_shift(0);
}

void reset_sheet() {
    settle_completions();
    if (sheet) lv_anim_delete(sheet, shift_anim);
    sliding = false;
    pending_view = -1;
    bitmaps::unfreeze();
    bitmaps::scroll_hide();
    task_page::destroy(other);
    task_page::destroy(current);
    touch();
    if (!sheet) return;
    lv_obj_set_width(sheet, page_width());
    lv_obj_set_style_translate_x(sheet, 0, 0);
    gesture.strip_dir = 0;
    gesture.strip_rest = 0;
    task_page::create(current, sheet, page_width());
}

void show_view(int view) {
    reset_sheet();
    active = view;
    task_page::paint(current, view, on_mark);
    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    select_tab(view, true);
    announce();
}

// Same view, new data: rebuild in place and keep the reading position.
void repaint() {
    Timed timed(stats.repaint_us);
    if (active >= 0) bitmaps::preview_drop(active);
    int32_t y = lv_obj_get_scroll_y(list);
    int view = current.view;
    reset_sheet();
    task_page::paint(current, view, on_mark);
    if (y > 0) {
        task_page::grow(current, kMaxTasks, on_mark);
        lv_obj_update_layout(list);
        int32_t bottom = lv_obj_get_scroll_bottom(list);
        lv_obj_scroll_to_y(list, y + (bottom < 0 ? bottom : 0), LV_ANIM_OFF);
    }
    announce();
}

uint32_t glide_time(int32_t from, int32_t to) {
    uint32_t duration = static_cast<uint32_t>(gap(to - from)) / 2u;
    if (duration < 140) duration = 140;
    if (duration > 260) duration = 260;
    return duration;
}

void play_shift(int32_t to, lv_anim_completed_cb_t done) {
    int32_t from = shift();
    lv_anim_delete(sheet, shift_anim);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, sheet);
    lv_anim_set_exec_cb(&anim, shift_anim);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_duration(&anim, from == to ? 0 : glide_time(from, to));
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    if (done) lv_anim_set_completed_cb(&anim, done);
    lv_anim_start(&anim);
}

const lv_draw_buf_t *ready_preview(int view) {
    lv_area_t area;
    lv_obj_get_coords(list, &area);
    return bitmaps::preview(view, task_store::version(view), lv_area_get_width(&area), lv_area_get_height(&area));
}

// Neighbour strip for a horizontal swipe. Prefer a fast freeze when both sides already have
// previews — never block the gesture on a synchronous full-list render (that was locking the drag).
void build_strip(int target) {
    Timed timed(stats.strip_us);
    drop_other();
    int dir = target > active ? 1 : -1;
    int32_t width = page_width();
    gesture.strip_dir = dir;
    gesture.strip_rest = dir > 0 ? 0 : -width;
    const lv_draw_buf_t *other_preview = ready_preview(target);
    const lv_draw_buf_t *page_preview =
        lv_obj_get_scroll_y(list) == 0 ? ready_preview(active) : nullptr;

    if (other_preview && page_preview) {
        bitmaps::FreezeSpec spec = {};
        spec.list = list;
        spec.page = current.obj;
        spec.version = epoch;
        spec.rest = gesture.strip_rest;
        spec.other_ref = dir > 0 ? -width : 0;
        spec.place_sheet = place_sheet;
        spec.other_bitmap = other_preview;
        spec.page_bitmap = page_preview;
        spec.page_shift = 0;
        if (bitmaps::freeze(spec)) return;
    }

    // Live path: build the neighbour page and slide the sheet — stays responsive under the finger.
    task_page::create(other, sheet, width);
    task_page::paint(other, target, on_mark);
    if (dir < 0) lv_obj_move_to_index(other.obj, 0);
    lv_obj_set_width(sheet, width * 2);
    set_shift(gesture.strip_rest);
}

void landed(lv_anim_t *) {
    Timed timed(stats.land_us);
    int view = pending_view;
    task_page::retire(current);
    if (other.obj) {
        current = other;
        other = task_page::Page{};
    } else {
        task_page::create(current, sheet, page_width());
        task_page::paint(current, view, on_mark);
    }
    bitmaps::unfreeze();
    touch();
    lv_obj_set_width(sheet, page_width());
    lv_obj_set_style_translate_x(sheet, 0, 0);
    if (current.obj) lv_obj_set_style_translate_y(current.obj, 0, 0);
    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    gesture.strip_dir = 0;
    gesture.strip_rest = 0;
    pending_view = -1;
    sliding = false;
    active = view;
    select_tab(view, true);
    announce();
    if (hooks.on_view_shown) hooks.on_view_shown(view);
}

void cancelled(lv_anim_t *) {
    drop_other();
    sliding = false;
}

// Horizontal slide between lists (swipe settle or chip tap) — one page width.
void go_to(int view) {
    if (sliding || view < 0 || view >= task_store::view_count()) return;
    if (view == active) {
        if (hooks.on_view_shown) hooks.on_view_shown(view);
        return;
    }
    sliding = true;
    pending_view = view;
    select_tab(view, false);  // chip reacts immediately; scroll settles on land
    build_strip(view);
    play_shift(view > active ? -page_width() : 0, landed);
}

void on_tab(lv_event_t *event) {
    go_to(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event))));
}

void on_refresh(lv_event_t *event) {
    if (hooks.on_refresh) hooks.on_refresh(event);
}

void on_back(lv_event_t *event) {
    if (hooks.on_back) hooks.on_back(event);
}

void deliver_done(void *) {
    while (done_count > 0) {
        lv_obj_t *row = done_rows[0];
        --done_count;
        for (int i = 0; i < done_count; ++i) done_rows[i] = done_rows[i + 1];
        if (completing > 0) --completing;
        int index = static_cast<int>(lv_obj_get_index(row));
        if (hooks.on_task_completed && index >= 0) hooks.on_task_completed(current.view, index);
    }
}

// The row is removed outside the animation callback; deleting an animation's target from inside it is unsafe.
void completed(lv_anim_t *anim) {
    if (done_count < kMaxDone) done_rows[done_count++] = static_cast<lv_obj_t *>(anim->var);
    lv_async_call(deliver_done, nullptr);
}

void on_mark(lv_event_t *event) {
    if (sliding || gesture.page_drag || gesture.suppress_click) return;
    lv_obj_t *mark = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    if (task_page::row_of(current, mark) < 0) return;
    bitmaps::scroll_hide();
    touch();
    ++completing;
    if (hooks.on_task_checked) hooks.on_task_checked();
    task_page::animate_completion(mark, completed, nullptr);
}

void on_pressed(lv_event_t *) {
    lv_indev_t *indev = lv_indev_active();
    if (sliding || completing || !indev) return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    if (gesture.strip_dir != 0 || bitmaps::frozen() || other.obj) drop_other();
    gesture.tracking = true;
    gesture.page_drag = false;
    gesture.suppress_click = false;
    gesture.x = point.x;
    gesture.y = point.y;
}

void begin_page_drag() {
    gesture.page_drag = true;
    gesture.suppress_click = true;
    bitmaps::scroll_hide();
    // Pin the current scroll so LVGL's vertical throw does not fight the horizontal slide.
    if (list) {
        lv_obj_scroll_to_y(list, lv_obj_get_scroll_y(list), LV_ANIM_OFF);
        lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    }
}

void on_pressing(lv_event_t *) {
    lv_indev_t *indev = lv_indev_active();
    int views = task_store::view_count();
    if (!gesture.tracking || sliding || completing || views <= 1 || !indev) return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    int dx = point.x - gesture.x;
    int dy = point.y - gesture.y;
    if (!gesture.page_drag) {
        if (gap(dx) < kDragLock && gap(dy) < kDragLock) return;
        if (gap(dy) > gap(dx)) {
            // Vertical wins — hand the gesture back to LVGL scrolling.
            gesture.tracking = false;
            return;
        }
        begin_page_drag();
        gesture.x = point.x;
        gesture.y = point.y;
        dx = 0;
    }
    if (gesture.strip_dir == 0) {
        if (dx <= -kStripStart && active + 1 < views) build_strip(active + 1);
        else if (dx >= kStripStart && active > 0) build_strip(active - 1);
        else {
            int x = dx;
            if ((dx > 0 && active <= 0) || (dx < 0 && active + 1 >= views)) x = dx / 5;
            set_shift(x);
            return;
        }
        // Strip just built at rest — apply the finger offset accumulated for this frame.
        dx = point.x - gesture.x;
    }
    int32_t x = gesture.strip_rest + dx;
    if (gesture.strip_dir > 0 && x > 0) x = 0;
    if (gesture.strip_dir < 0 && x < gesture.strip_rest) x = gesture.strip_rest;
    set_shift(x);
}

void on_released(lv_event_t *) {
    if (!gesture.tracking && !gesture.page_drag) return;
    gesture.tracking = false;
    if (list) lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    if (!gesture.page_drag) return;
    gesture.page_drag = false;
    int32_t x = shift();
    int32_t width = page_width();
    int32_t threshold = width / 6 < 36 ? 36 : width / 6;
    if (gesture.strip_dir > 0 && x <= -threshold) {
        sliding = true;
        pending_view = active + 1;
        select_tab(pending_view, false);
        play_shift(-width, landed);
    } else if (gesture.strip_dir < 0 && x >= gesture.strip_rest + threshold) {
        sliding = true;
        pending_view = active - 1;
        select_tab(pending_view, false);
        play_shift(0, landed);
    } else if (gesture.strip_dir != 0) {
        sliding = true;
        play_shift(gesture.strip_rest, cancelled);
    } else {
        play_shift(0, nullptr);
    }
}

void on_scroll_begin(lv_event_t *) {
    if (!gesture.page_drag) bitmaps::scroll_show(list, current.obj, epoch);
}

void on_scroll(lv_event_t *) { bitmaps::scroll_place(list, current.obj); }

void on_scroll_end(lv_event_t *) { bitmaps::scroll_hide(); }

bool busy() { return sliding || completing || gesture.tracking || bitmaps::scroll_visible(); }

void grow_step(lv_timer_t *) {
    if (!root || busy() || !task_page::growing(current)) return;
    if (task_page::grow(current, kGrowRows, on_mark)) touch();
}

// Draws `view` the way the list shows it at the top, on an off-screen twin of the list.
void render_preview(int view) {
    Timed timed(stats.preview_us);
    lv_area_t area;
    lv_obj_get_coords(list, &area);
    int32_t w = lv_area_get_width(&area);
    int32_t h = lv_area_get_height(&area);
    lv_draw_buf_t *buf = bitmaps::preview_target(view, w, h);
    if (!buf) return;
    lv_obj_t *stage = kit::bare(lv_layer_top());
    lv_obj_set_size(stage, w, h);
    lv_obj_set_pos(stage, -w - 64, area.y1);
    lv_obj_set_style_bg_color(stage, theme::background(), 0);
    lv_obj_set_style_bg_opa(stage, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(stage, theme::ink(), 0);
    lv_obj_set_style_pad_hor(stage, theme::kListPad, 0);
    task_page::Page page;
    task_page::create(page, stage, page_width());
    task_page::paint(page, view, on_mark);
    lv_obj_update_layout(stage);
    while (task_page::growing(page) && lv_obj_get_height(page.obj) < h) {
        task_page::grow(page, kGrowRows, on_mark);
        lv_obj_update_layout(stage);
    }
    lv_area_t stage_area;
    lv_obj_get_coords(stage, &stage_area);
    kit::draw_into(stage, buf, stage_area, stage_area);
    lv_image_cache_drop(buf);
    bitmaps::preview_ready(view, task_store::version(view));
    lv_obj_delete(stage);
}

// Neighbours first, then the rest outward. Lists still loading keep their live spinner instead.
bool preview_step() {
    int count = task_store::view_count();
    // The current list must be pre-rendered too, or slides mix live capture with neighbour previews.
    if (list && active >= 0 && active < count && lv_obj_get_scroll_y(list) == 0 &&
        task_store::state(active) != task_store::State::Empty && !ready_preview(active)) {
        render_preview(active);
        return true;
    }
    for (int d = 1; d < count; ++d) {
        for (int view : {active + d, active - d}) {
            if (view < 0 || view >= count || task_store::state(view) == task_store::State::Empty) continue;
            if (ready_preview(view)) continue;
            render_preview(view);
            return true;
        }
    }
    return false;
}

bool idle_gate_ok() { return !hooks.idle_allowed || hooks.idle_allowed(); }

// Idle housekeeping, one job per tick: apply data that arrived while the user was busy, finish building the
// page, pre-render it for scrolling, then pre-render the other lists for sliding.
void idle_step(lv_timer_t *) {
    if (!root || busy() || !current.obj || !idle_gate_ok()) return;
    if (task_page::reap()) return;
    if (current.version != task_store::version(current.view)) {
        repaint();
        return;
    }
    if (task_page::growing(current)) return;
    uint32_t quiet = lv_display_get_inactive_time(nullptr);
    if (quiet < kCacheQuietMs) return;
    bitmaps::scroll_step(list, current.obj, epoch);
    if (!bitmaps::scroll_ready(list, current.obj, epoch)) return;
    if (quiet < kPreviewQuietMs) return;
    preview_step();
}

void build_header(lv_obj_t *screen) {
    kit::SubsysBar bar =
        kit::subsys_bar(screen, LV_SYMBOL_LEFT, on_back, palette::widget_todoist(), on_refresh, LV_SYMBOL_REFRESH);
    header = bar.root;
    title_label = bar.title;
    status_label = bar.status;
    lv_obj_add_flag(title_label, LV_OBJ_FLAG_HIDDEN);
}

void build_list(lv_obj_t *screen) {
    tabs = lv_obj_create(screen);
    lv_obj_remove_style_all(tabs);
    lv_obj_set_width(tabs, lv_pct(100));
    lv_obj_set_height(tabs, 40);
    lv_obj_set_style_pad_hor(tabs, 10, 0);
    lv_obj_set_style_pad_column(tabs, 6, 0);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tabs, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(tabs, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(tabs, LV_SCROLLBAR_MODE_OFF);

    list = lv_obj_create(screen);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_hor(list, theme::kListPad, 0);
    lv_obj_set_style_pad_bottom(list, theme::kListPad, 0);
    lv_obj_set_style_text_color(list, theme::ink(), 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(list, theme::muted(), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_40, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list, 2, LV_PART_SCROLLBAR);
    lv_obj_add_event_cb(list, on_pressed, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(list, on_pressing, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(list, on_released, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(list, on_released, LV_EVENT_PRESS_LOST, nullptr);
    lv_obj_add_event_cb(list, on_scroll_begin, LV_EVENT_SCROLL_BEGIN, nullptr);
    lv_obj_add_event_cb(list, on_scroll, LV_EVENT_SCROLL, nullptr);
    lv_obj_add_event_cb(list, on_scroll_end, LV_EVENT_SCROLL_END, nullptr);

    sheet = kit::bare(list);
    lv_obj_set_style_text_color(sheet, theme::ink(), 0);
    lv_obj_set_width(sheet, page_width());
    lv_obj_set_height(sheet, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sheet, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_EVENT_BUBBLE);
    task_page::create(current, sheet, page_width());
}

bool timers_started = false;

void destroy_ui() {
    if (root) {
        lv_obj_delete(root);
        root = nullptr;
    }
    header = nullptr;
    title_label = nullptr;
    status_label = nullptr;
    tabs = nullptr;
    list = nullptr;
    sheet = nullptr;
    current = {};
    other = {};
    sliding = false;
    completing = 0;
    done_count = 0;
    for (int i = 0; i < kMaxDone; ++i) done_rows[i] = nullptr;
    gesture = {};
    ++epoch;
}

void build_into(lv_obj_t *parent) {
    if (!parent) return;
    destroy_ui();
    // Rebuild into the slot — no long-lived reparent of a parked tree.
    root = lv_obj_create(parent);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(root, theme::paper(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(root, &theme::title_font(), 0);
    lv_obj_set_style_text_color(root, theme::ink(), 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_pad_row(root, 0, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    build_header(root);
    build_list(root);
    if (header) lv_obj_set_height(header, LV_SIZE_CONTENT);
}

}  // namespace

void todoist_panel::begin(const Hooks &h) {
    hooks = h;
    if (!timers_started) {
        lv_timer_create(grow_step, 16, nullptr);
        lv_timer_create(idle_step, 25, nullptr);
        timers_started = true;
    }
}

void todoist_panel::show() {
    if (!standalone) {
        standalone = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(standalone, theme::paper(), 0);
        lv_obj_set_style_bg_opa(standalone, LV_OPA_COVER, 0);
        lv_obj_set_flex_flow(standalone, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(standalone, 0, 0);
        lv_obj_remove_flag(standalone, LV_OBJ_FLAG_SCROLLABLE);
    }
    int restore = active >= 0 ? active : 0;
    build_into(standalone);
    set_views(restore);
    lv_screen_load(standalone);
}

void todoist_panel::attach(lv_obj_t *parent) {
    if (!parent) {
        destroy_ui();
        return;
    }
    int restore = active >= 0 ? active : 0;
    build_into(parent);
    set_views(restore);
}

lv_obj_t *todoist_panel::screen() { return root; }

void todoist_panel::set_views(int selected) {
    int count = task_store::view_count();
    if (selected < 0 || selected >= count) selected = 0;
    active = selected;
    // begin() no longer builds UI — attach/show will apply when tabs exist.
    if (!tabs) return;
    lv_obj_clean(tabs);
    for (int i = 0; i < count; ++i) {
        lv_obj_t *chip = kit::chip(tabs, task_store::view(i).name, i == selected, on_tab,
                                   reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        kit::set_chip_tone(chip, palette::widget_todoist());
        kit::set_chip_selected(chip, i == selected, false);
    }
    show_view(selected);
}

void todoist_panel::view_changed(int view) {
    if (view == current.view && !busy() && current.version != task_store::version(view)) repaint();
}

void todoist_panel::drop_row(int view, int row) {
    if (view != current.view) return;
    task_page::remove_row(current, row);
    touch();
    announce();
}

void todoist_panel::set_status(const char *text) {
    if (!status_label) return;
    lv_label_set_text(status_label, text ? text : "");
    if (text && text[0]) lv_obj_remove_flag(status_label, LV_OBJ_FLAG_HIDDEN);
}

void todoist_panel::set_body_hidden(bool hidden) {
    if (hidden) {
        if (tabs) lv_obj_add_flag(tabs, LV_OBJ_FLAG_HIDDEN);
        if (list) lv_obj_add_flag(list, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (tabs) lv_obj_remove_flag(tabs, LV_OBJ_FLAG_HIDDEN);
    if (list) lv_obj_remove_flag(list, LV_OBJ_FLAG_HIDDEN);
    announce();
}

void todoist_panel::apply_theme() {
    bitmaps::invalidate();
    touch();
    if (!root) return;
    lv_obj_set_style_bg_color(root, theme::paper(), 0);
    lv_obj_set_style_text_color(root, theme::ink(), 0);
    if (title_label) lv_obj_set_style_text_color(title_label, theme::ink(), 0);
    if (status_label) lv_obj_set_style_text_color(status_label, theme::muted(), 0);
    if (current.view >= 0) show_view(current.view);
}

void todoist_panel::relayout() {
    if (header) lv_obj_set_height(header, LV_SIZE_CONTENT);
    if (current.view >= 0) show_view(current.view);
    else reset_sheet();
}

int todoist_panel::active_view() { return active; }

bool todoist_panel::idle() { return !busy(); }

todoist_panel::Stats todoist_panel::take_stats() {
    Stats out = stats;
    out.sliding = sliding;
    out.tracking = gesture.tracking;
    out.completing = completing;
    stats = Stats{};
    return out;
}
