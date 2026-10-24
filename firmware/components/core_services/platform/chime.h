#pragma once

#include "hal/board.h"

// The short "task done" tone, streamed to the board's sound port without blocking.
namespace chime {

void begin(hal::SoundPort *port);
void set_enabled(bool enabled);
void play();
void pump();

}  // namespace chime
