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

struct Perf {
    uint32_t frames;
    uint64_t push_us;
    uint64_t refr_us;
    uint64_t refr_max;
    uint64_t tick_max;
    uint64_t since;
    int64_t refr_start;
    bool pushed;
} perf;

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
               "heap_int=%u psram=%u\n",
               static_cast<int>(esp_reset_reason()), static_cast<unsigned>(perf.frames),
               span ? perf.frames * 1e6 / span : 0.0, perf.refr_us / 1000.0 / frames_n, perf.refr_max / 1000.0,
               perf.tick_max / 1000.0, perf.push_us / 1000.0 / frames_n, static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
               static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    perf = Perf{};
    perf.since = now;
}
