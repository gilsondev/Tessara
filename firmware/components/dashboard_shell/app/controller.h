#pragma once

#include "hal/board.h"

// Wires the board, the network worker, the task store and the screen. Everything here runs on the UI loop.
namespace app {

void begin(hal::Board &board);
void loop();

}  // namespace app
