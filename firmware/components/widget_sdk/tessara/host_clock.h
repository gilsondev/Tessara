#pragma once

#include <stdint.h>

namespace tessara {

// Monotonic ms for widgets (no Arduino include). Device binds via set_host_now_ms.
using HostNowMs = uint32_t (*)();
void set_host_now_ms(HostNowMs fn);
uint32_t host_now_ms();

}  // namespace tessara
