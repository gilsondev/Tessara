#include "ui/bitmaps.h"

#include "platform/display_host.h"
#include "ui/kit.h"
#include "ui/palette.h"
#include "ui/theme.h"

#include <esp_heap_caps.h>

#include <cstring>

namespace {
constexpr size_t kFreezeHalf = 480 * 320 * sizeof(uint16_t);
constexpr size_t kScrollBytes = 480 * 2600 * sizeof(uint16_t);
constexpr int32_t kStripeRows = 120;

struct Frozen {
    bool active;
    bool direct;      // frames composed in the framebuffer instead of moved by LVGL
    lv_obj_t *box;    // LVGL fallback only
    lv_obj_t *image[2];
    lv_obj_t *filler;
    int filler_side;
    int32_t step;
    int32_t origin;
    int32_t shift;
} freeze_state = {};
uint8_t *freeze_mem = nullptr;
lv_draw_buf_t freeze_bufs[2];

struct ScrollCache {
    uint8_t *mem;
    lv_draw_buf_t buf;
    lv_obj_t *page;
    uint32_t version;
    uint8_t theme;
    int32_t width;
    int32_t height;
    int32_t done;
    bool ready;
    lv_obj_t *box;
    lv_obj_t *image;
    lv_obj_t *thumb;
} cache = {};

constexpr int kMaxPreviews = 8;
struct Preview {
    uint8_t *mem;
    size_t capacity;
    lv_draw_buf_t buf;
    uint32_t version;
    int32_t width;
    int32_t height;
    bool ready;
    uint8_t theme;
} previews[kMaxPreviews] = {};

uint8_t *psram(size_t bytes) {
    return static_cast<uint8_t *>(heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

bool fresh(lv_obj_t *list, lv_obj_t *page, uint32_t version) {
    return cache.ready && page && cache.page == page && cache.version == version &&
           cache.theme == static_cast<uint8_t>(palette::mode()) &&
           cache.height == lv_obj_get_height(page) && cache.width == lv_obj_get_width(list);
}

// Draws the list viewport as it is now into fallback half `half` of the freeze memory.
const lv_draw_buf_t *render_list(lv_obj_t *list, int half) {
    if (!freeze_mem) freeze_mem = psram(kFreezeHalf * 2);
    if (!freeze_mem) return nullptr;
    lv_area_t area;
    lv_obj_get_coords(list, &area);
    int32_t w = lv_area_get_width(&area);
    int32_t h = lv_area_get_height(&area);
    uint32_t stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565);
    lv_draw_buf_t &buf = freeze_bufs[half];
    if (lv_draw_buf_init(&buf, w, h, LV_COLOR_FORMAT_RGB565, stride, freeze_mem + half * kFreezeHalf, stride * h) !=
        LV_RESULT_OK) {
        return nullptr;
    }
    kit::draw_into(list, &buf, area, area);
    lv_image_cache_drop(&buf);
    return &buf;
}
}  // namespace

void bitmaps::scroll_step(lv_obj_t *list, lv_obj_t *page, uint32_t version) {
    if (!list || !page || cache.box || freeze_state.box) return;
    int32_t width = lv_obj_get_width(list);
    int32_t height = lv_obj_get_height(page);
    if (cache.page != page || cache.version != version || cache.width != width || cache.height != height) {
        cache.page = page;
        cache.version = version;
        cache.width = width;
        cache.height = height;
        cache.done = 0;
        cache.ready = false;
        uint32_t stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);
        if (height <= 0 || stride * height > kScrollBytes) {
            cache.done = height;
            return;
        }
        if (!cache.mem) cache.mem = psram(kScrollBytes);
        if (!cache.mem || lv_draw_buf_init(&cache.buf, width, height, LV_COLOR_FORMAT_RGB565, stride, cache.mem,
                                           stride * height) != LV_RESULT_OK) {
            cache.done = height;
            return;
        }
    }
    if (cache.ready || cache.done >= cache.height) return;
    lv_area_t view;
    lv_area_t box;
    lv_obj_get_coords(list, &view);
    lv_obj_get_coords(page, &box);
    lv_area_t buf_area = {view.x1, box.y1, view.x1 + width - 1, box.y1 + height - 1};
    int32_t end = cache.done + kStripeRows;
    if (end > height) end = height;
    lv_area_t clip = {view.x1, box.y1 + cache.done, view.x1 + width - 1, box.y1 + end - 1};
    kit::draw_into(page, &cache.buf, buf_area, clip);
    cache.done = end;
    if (end >= height) {
        cache.ready = true;
        cache.theme = static_cast<uint8_t>(palette::mode());
        lv_image_cache_drop(&cache.buf);
    }
}

bool bitmaps::scroll_ready(lv_obj_t *list, lv_obj_t *page, uint32_t version) {
    return fresh(list, page, version) || (cache.page == page && cache.version == version && cache.done >= cache.height);
}

void bitmaps::scroll_place(lv_obj_t *list, lv_obj_t *page) {
    if (!cache.box || !page) return;
    lv_area_t view;
    lv_area_t box;
    lv_obj_get_coords(list, &view);
    lv_obj_get_coords(page, &box);
    lv_obj_set_y(cache.image, box.y1 - view.y1);
    int32_t view_h = lv_area_get_height(&view);
    int32_t content_h = lv_obj_get_scroll_top(list) + view_h + lv_obj_get_scroll_bottom(list);
    if (content_h <= view_h) content_h = view_h + 1;
    int32_t thumb_h = view_h * view_h / content_h;
    if (thumb_h < 24) thumb_h = 24;
    int32_t span = content_h - view_h;
    int32_t top = lv_obj_get_scroll_top(list);
    if (top < 0) top = 0;
    if (top > span) top = span;
    lv_obj_set_y(cache.thumb, top * (view_h - thumb_h) / span);
    lv_obj_set_height(cache.thumb, thumb_h);
}

void bitmaps::scroll_show(lv_obj_t *list, lv_obj_t *page, uint32_t version) {
    if (cache.box || freeze_state.box || !fresh(list, page, version)) return;
    lv_area_t view;
    lv_obj_get_coords(list, &view);
    cache.box = kit::bare(lv_screen_active());
    lv_obj_add_flag(cache.box, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(cache.box, view.x1, view.y1);
    lv_obj_set_size(cache.box, lv_area_get_width(&view), lv_area_get_height(&view));
    lv_obj_set_style_bg_color(cache.box, theme::paper(), 0);
    lv_obj_set_style_bg_opa(cache.box, LV_OPA_COVER, 0);
    cache.image = kit::opaque_image(cache.box, &cache.buf);
    cache.thumb = kit::bare(cache.box);
    lv_obj_set_width(cache.thumb, 3);
    lv_obj_set_x(cache.thumb, lv_area_get_width(&view) - 3);
    lv_obj_set_style_radius(cache.thumb, 2, 0);
    lv_obj_set_style_bg_color(cache.thumb, theme::muted(), 0);
    lv_obj_set_style_bg_opa(cache.thumb, LV_OPA_40, 0);
    scroll_place(list, page);
}

void bitmaps::scroll_hide() {
    if (!cache.box) return;
    lv_obj_delete(cache.box);
    cache.box = cache.image = cache.thumb = nullptr;
}

bool bitmaps::scroll_visible() { return cache.box != nullptr; }

lv_draw_buf_t *bitmaps::preview(int view, uint32_t version, int32_t width, int32_t height) {
    if (view < 0 || view >= kMaxPreviews) return nullptr;
    Preview &slot = previews[view];
    bool ok = slot.ready && slot.version == version && slot.width == width && slot.height == height &&
              slot.theme == static_cast<uint8_t>(palette::mode());
    return ok ? &slot.buf : nullptr;
}

lv_draw_buf_t *bitmaps::preview_target(int view, int32_t width, int32_t height) {
    if (view < 0 || view >= kMaxPreviews || width <= 0 || height <= 0) return nullptr;
    Preview &slot = previews[view];
    slot.ready = false;
    uint32_t stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);
    size_t bytes = stride * height;
    if (bytes > slot.capacity) {
        heap_caps_free(slot.mem);
        slot.mem = psram(bytes);
        slot.capacity = slot.mem ? bytes : 0;
    }
    if (!slot.mem) return nullptr;
    if (lv_draw_buf_init(&slot.buf, width, height, LV_COLOR_FORMAT_RGB565, stride, slot.mem, bytes) != LV_RESULT_OK) {
        return nullptr;
    }
    slot.width = width;
    slot.height = height;
    return &slot.buf;
}

void bitmaps::preview_ready(int view, uint32_t version) {
    if (view < 0 || view >= kMaxPreviews) return;
    previews[view].version = version;
    previews[view].theme = static_cast<uint8_t>(palette::mode());
    previews[view].ready = true;
}

void bitmaps::preview_drop(int view) {
    if (view < 0 || view >= kMaxPreviews) return;
    previews[view].ready = false;
}

bool bitmaps::freeze(const FreezeSpec &spec) {
    unfreeze();
    lv_obj_t *list = spec.list;
    lv_obj_t *page = spec.page;
    int32_t rest = spec.rest;
    int32_t other_ref = spec.other_ref;
    if (!list || (!spec.other && !spec.other_bitmap)) return false;
    lv_obj_update_layout(lv_screen_active());
    lv_area_t area;
    lv_obj_get_coords(list, &area);
    int32_t w = lv_area_get_width(&area);
    int32_t h = lv_area_get_height(&area);
    uint32_t stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565);
    if (stride * h > kFreezeHalf) return false;

    // Each half shows an existing bitmap when there is one; only a missing one is drawn now.
    const lv_draw_buf_t *page_src = nullptr;
    int32_t page_y = 0;
    const int32_t scroll_y = lv_obj_get_scroll_y(list);
    // At scroll top, prefer the same viewport preview as the neighbour side. The vertical scroll
    // cache draws only the page object; that path tints text differently from render_preview().
    if (scroll_y == 0 && spec.page_bitmap && spec.page_bitmap->header.w == w &&
        spec.page_bitmap->header.h == h) {
        page_src = spec.page_bitmap;
    } else if (fresh(list, page, spec.version)) {
        lv_area_t box;
        lv_obj_get_coords(page, &box);
        page_src = &cache.buf;
        page_y = box.y1 - area.y1;
    } else if (spec.page_bitmap && spec.page_bitmap->header.w == w && spec.page_bitmap->header.h == h) {
        page_src = spec.page_bitmap;
    } else {
        spec.place_sheet(spec.page_shift);
        lv_obj_update_layout(lv_screen_active());
        page_src = render_list(list, 0);
    }
    const lv_draw_buf_t *other_src = spec.other_bitmap;
    if (!other_src || other_src->header.w != w || other_src->header.h != h) {
        other_src = nullptr;
        if (spec.other) {
            lv_obj_set_style_translate_y(spec.other, lv_obj_get_scroll_y(list), 0);
            spec.place_sheet(other_ref);
            lv_obj_update_layout(lv_screen_active());
            other_src = render_list(list, 1);
        }
    }
    spec.place_sheet(spec.page_shift);
    if (!page_src || !other_src) return false;

    bool other_left = other_ref > rest;
    int32_t step = other_ref > rest ? other_ref - rest : rest - other_ref;
    int page_side = other_left ? 1 : 0;
    freeze_state.active = true;
    freeze_state.step = step;
    freeze_state.origin = other_left ? other_ref : rest;

    // Page on the left half when the neighbour comes in from the right, and the other way round.
    display_host::SlideSpec slide = {};
    slide.viewport = area;
    slide.step = step;
    slide.left = page_side == 0 ? display_host::SlideHalf{page_src, page_y}
                                : display_host::SlideHalf{other_src, 0};
    slide.right = page_side == 0 ? display_host::SlideHalf{other_src, 0}
                                 : display_host::SlideHalf{page_src, page_y};
    slide.fill = lv_color_to_u16(theme::paper());
    freeze_state.direct = display_host::slide_begin(slide);

    if (!freeze_state.direct) {
        // LVGL fallback: both halves over the list, moved per frame.
        freeze_state.box = kit::bare(lv_screen_active());
        lv_obj_add_flag(freeze_state.box, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_pos(freeze_state.box, area.x1, area.y1);
        lv_obj_set_size(freeze_state.box, w, h);
        // Both halves (plus the filler under a short page) paint every pixel at any shift.
        kit::declare_opaque(freeze_state.box);
        freeze_state.image[1 - page_side] = kit::opaque_image(freeze_state.box, other_src);
        freeze_state.image[page_side] = kit::opaque_image(freeze_state.box, page_src);
        lv_obj_set_y(freeze_state.image[1 - page_side], 0);
        lv_obj_set_y(freeze_state.image[page_side], page_y);
        int32_t page_bottom = page_y + static_cast<int32_t>(page_src->header.h);
        if (page_bottom < h) {
            freeze_state.filler = kit::bare(freeze_state.box);
            lv_obj_set_size(freeze_state.filler, w, h - page_bottom);
            lv_obj_set_y(freeze_state.filler, page_bottom);
            lv_obj_set_style_bg_color(freeze_state.filler, theme::paper(), 0);
            lv_obj_set_style_bg_opa(freeze_state.filler, LV_OPA_COVER, 0);
            freeze_state.filler_side = page_side;
        }
    }
    set_frozen_shift(rest);
    return true;
}

bool bitmaps::frozen() { return freeze_state.active; }

void bitmaps::set_frozen_shift(int32_t shift) {
    if (!freeze_state.active) return;
    freeze_state.shift = shift;
    if (freeze_state.direct) {
        // Strip column sitting at the left edge of the viewport.
        display_host::slide_frame(freeze_state.origin - shift);
        return;
    }
    int32_t x[2] = {shift - freeze_state.origin, shift - freeze_state.origin + freeze_state.step};
    lv_obj_set_x(freeze_state.image[0], x[0]);
    lv_obj_set_x(freeze_state.image[1], x[1]);
    if (freeze_state.filler) lv_obj_set_x(freeze_state.filler, x[freeze_state.filler_side]);
}

int32_t bitmaps::frozen_shift() { return freeze_state.shift; }

void bitmaps::unfreeze() {
    if (!freeze_state.active) return;
    if (freeze_state.direct) display_host::slide_end();
    if (freeze_state.box) lv_obj_delete(freeze_state.box);
    freeze_state = Frozen{};
}

void bitmaps::invalidate() {
    unfreeze();
    scroll_hide();
    cache.ready = false;
    cache.done = 0;
    cache.page = nullptr;
    if (cache.mem) lv_image_cache_drop(&cache.buf);
    for (Preview &slot : previews) {
        slot.ready = false;
        if (slot.mem) lv_image_cache_drop(&slot.buf);
    }
}
