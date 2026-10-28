#pragma once

#include "hal/board.h"

#include <lvgl.h>

class Print;

// Owns LVGL's display: two PSRAM frames rendered in DIRECT mode while a core-0 task pushes the finished one.
namespace display_host {

bool begin(hal::DisplayPort &port, hal::BacklightPort *backlight, const hal::BoardDescriptor &board,
           hal::Rotation rotation);
void set_rotation(hal::Rotation rotation);
hal::Rotation rotation();
bool landscape();
void set_brightness(uint8_t percent);
// Stop the core-0 frame push. Flash (NVS, SD, Wi-Fi, mDNS) disables the cache;
// a push of a PSRAM frame in that window panics the chip. Balance every pause.
void pause_push();
void resume_push();
// True while the core-0 task holds the frame semaphore. Also true during pause_push;
// push_paused() separates that safe hold from a live DMA push.
bool push_busy();
bool push_paused();
// Renders pending changes and waits until they are on the glass.
void refresh_now();
// Streams the visible logical frame as "SNAP w h\n" + RGB888 rows + "\nSNAP END".
void capture(Print &out);
// Runs LVGL's timers (input, animations, rendering) and records how long the UI thread was busy.
void tick();
void report(Print &out);

// Slide fast path. A horizontal page slide is a rigid translation of two pre-rendered viewports,
// so a frame is a row copy instead of a redraw: LVGL's software renderer needs ~0.4 us/px on this
// board (~16 fps for a full viewport) while a PSRAM row copy runs ~20x faster.
// The two halves sit side by side in a virtual strip: `left` at column 0, `right` at `step`.
struct SlideHalf {
    const lv_draw_buf_t *buf;
    int32_t y_at;  // strip row where source row 0 lands (negative when the page is scrolled)
};
struct SlideSpec {
    lv_area_t viewport;  // screen area the slide owns; everything else stays put
    int32_t step;
    SlideHalf left;
    SlideHalf right;
    uint16_t fill;  // rows a half does not cover (page shorter than the viewport)
};
// slide_begin() gives both frames the parts of the screen that stay put; every slide_frame()
// rewrites the viewport from strip column `strip_x` and pushes. See ui/bitmaps.h.
bool slide_begin(const SlideSpec &spec);
void slide_frame(int32_t strip_x);
void slide_end();

}  // namespace display_host
