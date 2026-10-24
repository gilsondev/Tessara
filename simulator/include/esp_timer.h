#pragma once

#include <SDL.h>
#include <cstdint>

inline int64_t esp_timer_get_time() {
    return static_cast<int64_t>(SDL_GetTicks()) * 1000;
}
