#pragma once

#include <lvgl.h>
#include <stdint.h>

// Pre-rendered bitmaps that turn heavy redraws into plain copies:
// - scroll cache: the whole current page, rendered in idle stripes, shown while the list scrolls vertically;
// - freeze: current and neighbour viewports side by side, moved as one image while pages slide.
namespace bitmaps {

// `version` must change whenever the page's pixels may have changed.
void scroll_step(lv_obj_t *list, lv_obj_t *page, uint32_t version);
bool scroll_ready(lv_obj_t *list, lv_obj_t *page, uint32_t version);
void scroll_show(lv_obj_t *list, lv_obj_t *page, uint32_t version);
void scroll_place(lv_obj_t *list, lv_obj_t *page);
void scroll_hide();
bool scroll_visible();

// Viewport-sized picture of a list at its top, rendered ahead of time so a slide starts with a copy.
lv_draw_buf_t *preview(int view, uint32_t version, int32_t width, int32_t height);
lv_draw_buf_t *preview_target(int view, int32_t width, int32_t height);
void preview_ready(int view, uint32_t version);
void preview_drop(int view);

// Positions the live sheet; freeze() moves it to snapshot each half.
using ShiftFn = void (*)(int32_t shift);
struct FreezeSpec {
    lv_obj_t *list;
    lv_obj_t *page;
    uint32_t version;
    int32_t rest;           // sheet shift that shows `page`
    int32_t other_ref;      // sheet shift that shows the neighbour
    int32_t page_shift;     // sheet shift at which `page` renders when it has to be drawn live
    const lv_draw_buf_t *page_bitmap;  // viewport picture of `page` when the scroll cache is stale
    lv_obj_t *other;        // live neighbour page, or null when `other_bitmap` is given
    const lv_draw_buf_t *other_bitmap;
    ShiftFn place_sheet;
};
bool freeze(const FreezeSpec &spec);
bool frozen();
void set_frozen_shift(int32_t shift);
int32_t frozen_shift();
void unfreeze();
// Drops slide and scroll pictures so the next gesture is drawn in the current theme.
void invalidate();

}  // namespace bitmaps
