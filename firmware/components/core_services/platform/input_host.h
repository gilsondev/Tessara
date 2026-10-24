#pragma once

#include "hal/board.h"

#include <Print.h>
#include <stdint.h>

// LVGL pointer input fed by the board's touch port, filtered for capacitive glitches, plus the hardware key.
namespace input_host {

void begin(hal::TouchPort *touch, hal::ButtonPort *button, const hal::BoardDescriptor &board);
// Scripted gesture in logical (rotated) coordinates, used by the USB probe.
void inject_drag(int x0, int y0, int x1, int y1, uint32_t duration_ms);
// True once per short press of the hardware key.
bool take_key_tap();
void report(Print &out);
// While resting, the next press wakes via on_wake and is not delivered to LVGL until release.
void set_resting(bool resting);
void set_wake_handler(void (*on_wake)());
void set_activity_handler(void (*on_activity)());

}  // namespace input_host
