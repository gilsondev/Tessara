#pragma once

#include "hal/board.h"

class Print;

// Owns LVGL's display: two PSRAM frames rendered in DIRECT mode while a core-0 task pushes the finished one.
namespace display_host {

bool begin(hal::DisplayPort &port, hal::BacklightPort *backlight, const hal::BoardDescriptor &board,
           hal::Rotation rotation);
void set_rotation(hal::Rotation rotation);
hal::Rotation rotation();
bool landscape();
void set_brightness(uint8_t percent);
// Renders pending changes and waits until they are on the glass.
void refresh_now();
// Streams the visible logical frame as "SNAP w h\n" + RGB888 rows + "\nSNAP END".
void capture(Print &out);
// Runs LVGL's timers (input, animations, rendering) and records how long the UI thread was busy.
void tick();
void report(Print &out);

}  // namespace display_host
