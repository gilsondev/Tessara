#include "platform/agent_push.h"

#include <Arduino.h>

namespace {

constexpr uint32_t kBootWindowMs = 3UL * 60UL * 1000UL;
constexpr uint32_t kArmWindowMs = 5UL * 60UL * 1000UL;

bool g_armed = false;
uint32_t g_until_ms = 0;

bool still_open() {
    if (!g_armed) return false;
    if (g_until_ms == 0) return true;
    if (static_cast<int32_t>(millis() - g_until_ms) >= 0) {
        g_armed = false;
        g_until_ms = 0;
        return false;
    }
    return true;
}

}  // namespace

void agent_push::arm() {
    g_armed = true;
    g_until_ms = millis() + kArmWindowMs;
}

void agent_push::disarm() {
    g_armed = false;
    g_until_ms = 0;
}

bool agent_push::armed() { return still_open(); }

void agent_push::arm_boot() {
    g_armed = true;
    g_until_ms = millis() + kBootWindowMs;
}
