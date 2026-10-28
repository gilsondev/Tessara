#include "platform/display_host.h"

#include "platform/log.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lvgl.h>

#include <cstring>

namespace {
hal::DisplayPort *port = nullptr;
hal::BacklightPort *backlight = nullptr;
const hal::BoardDescriptor *board = nullptr;
lv_display_t *display = nullptr;
hal::Rotation current = hal::Rotation::R0;
uint16_t *frames[2] = {};
size_t frame_bytes = 0;
uint16_t *volatile front = nullptr;
volatile hal::Rotation push_rotation = hal::Rotation::R0;
TaskHandle_t push_task = nullptr;
SemaphoreHandle_t push_idle = nullptr;
bool pause_held = false;

struct Perf {
    uint32_t frames;
    uint64_t push_us;
    uint64_t refr_us;
    uint64_t refr_max;
    uint64_t tick_max;
    uint64_t since;
    int64_t refr_start;
    uint32_t slides;
    uint64_t slide_us;
    bool pushed;
} perf;

bool slide_active = false;
display_host::SlideSpec slide = {};

lv_display_rotation_t to_lvgl(hal::Rotation rotation) {
    switch (rotation) {
        case hal::Rotation::R90: return LV_DISPLAY_ROTATION_90;
        case hal::Rotation::R180: return LV_DISPLAY_ROTATION_180;
        case hal::Rotation::R270: return LV_DISPLAY_ROTATION_270;
        default: return LV_DISPLAY_ROTATION_0;
    }
}

bool sideways(hal::Rotation rotation) { return rotation == hal::Rotation::R90 || rotation == hal::Rotation::R270; }

int16_t logical_width() { return sideways(current) ? board->height : board->width; }
int16_t logical_height() { return sideways(current) ? board->width : board->height; }

void push_loop(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int64_t start = esp_timer_get_time();
        hal::Rotation rotation = push_rotation;
        bool wide = sideways(rotation);
        port->push(front, wide ? board->height : board->width, wide ? board->width : board->height, rotation);
        perf.push_us += static_cast<uint64_t>(esp_timer_get_time() - start);
        perf.frames++;
        xSemaphoreGive(push_idle);
    }
}

// In DIRECT mode the last flush hands over the whole finished frame; LVGL then draws into the other one.
void flush(lv_display_t *disp, const lv_area_t *, uint8_t *pixels) {
    if (pixels && lv_display_flush_is_last(disp)) {
        xSemaphoreTake(push_idle, portMAX_DELAY);
        front = reinterpret_cast<uint16_t *>(pixels);
        push_rotation = current;
        perf.pushed = true;
        xTaskNotifyGive(push_task);
    }
    lv_display_flush_ready(disp);
}

void on_refr(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_REFR_START) {
        perf.refr_start = esp_timer_get_time();
        return;
    }
    if (!perf.refr_start || !perf.pushed) return;
    perf.pushed = false;
    uint64_t spent = static_cast<uint64_t>(esp_timer_get_time() - perf.refr_start);
    perf.refr_start = 0;
    perf.refr_us += spent;
    if (spent > perf.refr_max) perf.refr_max = spent;
}

void wait_push() {
    xSemaphoreTake(push_idle, portMAX_DELAY);
    xSemaphoreGive(push_idle);
}

void bind_buffers() {
    wait_push();
    xSemaphoreTake(push_idle, portMAX_DELAY);
    lv_display_set_rotation(display, to_lvgl(current));
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, frames[0], frames[1], frame_bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
    xSemaphoreGive(push_idle);
    lv_obj_invalidate(lv_screen_active());
}
}  // namespace

bool display_host::begin(hal::DisplayPort &display_port, hal::BacklightPort *backlight_port,
                         const hal::BoardDescriptor &descriptor, hal::Rotation rotation) {
    port = &display_port;
    backlight = backlight_port;
    board = &descriptor;
    current = rotation;
    if (!port->begin()) return false;

    frame_bytes = static_cast<size_t>(board->width) * board->height * sizeof(uint16_t);
    for (uint16_t *&slot : frames) {
        slot = static_cast<uint16_t *>(heap_caps_aligned_alloc(64, frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!slot) return false;
        memset(slot, 0xFF, frame_bytes);
    }
    push_idle = xSemaphoreCreateBinary();
    if (!push_idle) return false;
    front = frames[0];
    port->push(front, logical_width(), logical_height(), current);
    xSemaphoreGive(push_idle);
    xTaskCreatePinnedToCore(push_loop, "push", 4096, nullptr, 5, &push_task, 0);

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    display = lv_display_create(board->width, board->height);
    lv_display_set_flush_cb(display, flush);
    lv_display_add_event_cb(display, on_refr, LV_EVENT_REFR_START, nullptr);
    lv_display_add_event_cb(display, on_refr, LV_EVENT_REFR_READY, nullptr);
    bind_buffers();
    perf.since = esp_timer_get_time();
    return true;
}

void display_host::set_rotation(hal::Rotation rotation) {
    if (!display || rotation == current) return;
    current = rotation;
    bind_buffers();
}

hal::Rotation display_host::rotation() { return current; }

void display_host::set_brightness(uint8_t percent) {
    if (backlight) backlight->set_brightness(percent);
}

void display_host::pause_push() {
    if (!push_idle) return;
    xSemaphoreTake(push_idle, portMAX_DELAY);
    pause_held = true;
}

void display_host::resume_push() {
    if (!push_idle) return;
    xSemaphoreGive(push_idle);
    pause_held = false;
}

bool display_host::push_busy() {
    if (!push_idle) return false;
    return uxSemaphoreGetCount(push_idle) == 0;
}

bool display_host::push_paused() { return pause_held; }

bool display_host::landscape() { return board && logical_width() > logical_height(); }

void display_host::refresh_now() {
    if (!display) return;
    lv_refr_now(display);
    wait_push();
}

void display_host::capture(Print &out) {
    if (!front) return;
    logger::lock();
    xSemaphoreTake(push_idle, portMAX_DELAY);
    int16_t w = logical_width();
    int16_t h = logical_height();
    out.printf("SNAP %d %d\n", w, h);
    uint8_t row[480 * 3];
    for (int y = 0; y < h; ++y) {
        const uint16_t *src = front + y * w;
        for (int x = 0; x < w && x < 480; ++x) {
            uint16_t pixel = src[x];
            row[x * 3] = static_cast<uint8_t>(((pixel >> 11) & 31) << 3);
            row[x * 3 + 1] = static_cast<uint8_t>(((pixel >> 5) & 63) << 2);
            row[x * 3 + 2] = static_cast<uint8_t>((pixel & 31) << 3);
        }
        out.write(row, w * 3);
        // USB CDC + full-frame RGB888 can stall the TX ring; yield so Wi‑Fi/UI keep running
        // and the task watchdog does not fire mid-SNAP.
        if ((y & 7) == 7) {
            out.flush();
            vTaskDelay(1);
        }
    }
    out.write('\n');
    out.println("SNAP END");
    out.flush();
    xSemaphoreGive(push_idle);
    logger::unlock();
}

bool display_host::slide_begin(const SlideSpec &spec) {
    if (!display || !frames[0] || !frames[1] || !front) return false;
    if (!spec.left.buf || !spec.right.buf || !spec.left.buf->data || !spec.right.buf->data) return false;
    int32_t stride_px = logical_width();
    int32_t rows = logical_height();
    int32_t vw = lv_area_get_width(&spec.viewport);
    int32_t vh = lv_area_get_height(&spec.viewport);
    // step may be shorter than the viewport: the halves then overlap by the page margin, and the
    // right one owns the overlap just as the later LVGL child does.
    if (vw <= 0 || vh <= 0 || spec.step <= 0) return false;
    if (spec.viewport.x1 < 0 || spec.viewport.y1 < 0) return false;
    if (spec.viewport.x1 + vw > stride_px || spec.viewport.y1 + vh > rows) return false;
    if (static_cast<int32_t>(spec.left.buf->header.w) < vw) return false;
    if (static_cast<int32_t>(spec.right.buf->header.w) < vw) return false;

    // The spare frame is not being pushed, so this needs no wait. Copy only what slide_frame()
    // will not rewrite: the bands above and below the viewport, plus any columns beside it.
    uint16_t *spare = front == frames[0] ? frames[1] : frames[0];
    const uint16_t *live = front;
    int32_t y2 = spec.viewport.y1 + vh - 1;
    int32_t x2 = spec.viewport.x1 + vw - 1;
    for (int32_t y = 0; y < rows; ++y) {
        uint16_t *dst = spare + y * stride_px;
        const uint16_t *src = live + y * stride_px;
        if (y < spec.viewport.y1 || y > y2) {
            memcpy(dst, src, static_cast<size_t>(stride_px) * sizeof(uint16_t));
            continue;
        }
        if (spec.viewport.x1 > 0) memcpy(dst, src, static_cast<size_t>(spec.viewport.x1) * sizeof(uint16_t));
        if (x2 + 1 < stride_px) {
            memcpy(dst + x2 + 1, src + x2 + 1, static_cast<size_t>(stride_px - x2 - 1) * sizeof(uint16_t));
        }
    }
    slide = spec;
    slide_active = true;
    return true;
}

void display_host::slide_frame(int32_t strip_x) {
    if (!slide_active) return;
    int64_t compose_start = esp_timer_get_time();
    int32_t stride_px = logical_width();
    int32_t vw = lv_area_get_width(&slide.viewport);
    int32_t vh = lv_area_get_height(&slide.viewport);
    int32_t span = slide.step + vw;  // strip columns the two halves cover
    if (strip_x < 0) strip_x = 0;
    if (strip_x > span - vw) strip_x = span - vw;

    uint16_t *next = front == frames[0] ? frames[1] : frames[0];
    for (int32_t y = 0; y < vh; ++y) {
        uint16_t *dst = next + (slide.viewport.y1 + y) * stride_px + slide.viewport.x1;
        int32_t column = strip_x;
        int32_t done = 0;
        while (done < vw) {
            bool on_left = column < slide.step;
            const SlideHalf &half = on_left ? slide.left : slide.right;
            int32_t half_x1 = on_left ? 0 : slide.step;
            int32_t run = (on_left ? slide.step : span) - column;
            if (run > vw - done) run = vw - done;
            int32_t source_y = y - half.y_at;
            if (source_y >= 0 && source_y < static_cast<int32_t>(half.buf->header.h)) {
                const auto *rows = static_cast<const uint8_t *>(half.buf->data);
                const auto *src = reinterpret_cast<const uint16_t *>(rows + source_y * half.buf->header.stride);
                memcpy(dst + done, src + (column - half_x1), static_cast<size_t>(run) * sizeof(uint16_t));
            } else {
                for (int32_t i = 0; i < run; ++i) dst[done + i] = slide.fill;
            }
            done += run;
            column += run;
        }
    }
    perf.slides++;
    perf.slide_us += static_cast<uint64_t>(esp_timer_get_time() - compose_start);
    xSemaphoreTake(push_idle, portMAX_DELAY);
    front = next;
    push_rotation = current;
    xTaskNotifyGive(push_task);
}

void display_host::slide_end() {
    if (!slide_active) return;
    slide_active = false;
    wait_push();
    // LVGL still believes its frames hold what it drew last. Only the viewport changed under it,
    // so a full-screen invalidate would repaint the header and chips for nothing.
    if (display) lv_obj_invalidate_area(lv_screen_active(), &slide.viewport);
}

void display_host::tick() {
    int64_t start = esp_timer_get_time();
    lv_timer_handler();
    uint64_t spent = static_cast<uint64_t>(esp_timer_get_time() - start);
    if (spent > perf.tick_max) perf.tick_max = spent;
}

void display_host::report(Print &out) {
    uint64_t now = esp_timer_get_time();
    uint64_t span = now - perf.since;
    uint32_t frames_n = perf.frames ? perf.frames : 1;
    out.printf("PERF reset=%d frames=%u fps=%.1f refr_avg=%.1fms refr_max=%.1fms tick_max=%.1fms push_avg=%.1fms "
               "slides=%u slide_avg=%.1fms heap_int=%u psram=%u\n",
               static_cast<int>(esp_reset_reason()), static_cast<unsigned>(perf.frames),
               span ? perf.frames * 1e6 / span : 0.0, perf.refr_us / 1000.0 / frames_n, perf.refr_max / 1000.0,
               perf.tick_max / 1000.0, perf.push_us / 1000.0 / frames_n, static_cast<unsigned>(perf.slides),
               perf.slide_us / 1000.0 / (perf.slides ? perf.slides : 1),
               static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
               static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    perf = Perf{};
    perf.since = now;
}
