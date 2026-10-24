#pragma once

#include <SDL.h>
#include <cstdint>

inline uint32_t millis() { return static_cast<uint32_t>(SDL_GetTicks()); }
