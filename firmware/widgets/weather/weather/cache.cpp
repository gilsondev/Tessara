#include "weather/cache.h"

#include <cstring>

namespace weather {
namespace {

constexpr uint32_t kFreshMs = 1800000;

struct State {
    bool placed = false;
    bool ready = false;
    bool failed = false;
    uint32_t generation = 0;
    uint32_t stored_ms = 0;
    char name[48] = {};
    float latitude = 0;
    float longitude = 0;
    Forecast forecast = {};
};

State state;

void copy_name(char *dest, size_t cap, const char *src) {
    size_t n = 0;
    if (src) {
        while (src[n] && n + 1 < cap) {
            dest[n] = src[n];
            ++n;
        }
    }
    dest[n] = '\0';
}

bool fresh(uint32_t now_ms) {
    if (!state.ready) return false;
    if (now_ms < state.stored_ms) return false;
    return now_ms - state.stored_ms < kFreshMs;
}

}  // namespace

void reset() { state = {}; }

void set_place(const char *name, float latitude, float longitude) {
    state.placed = true;
    copy_name(state.name, sizeof(state.name), name);
    state.latitude = latitude;
    state.longitude = longitude;
    state.forecast = {};
    state.ready = false;
    state.failed = false;
    state.stored_ms = 0;
    state.generation += 1;
}

uint32_t note_request() { return state.generation; }

bool store(uint32_t generation, const Forecast &forecast, uint32_t now_ms) {
    if (!state.placed || generation != state.generation) return false;
    state.forecast = forecast;
    state.ready = true;
    state.failed = false;
    state.stored_ms = now_ms;
    return true;
}

void note_failure() {
    if (!state.placed) return;
    state.failed = true;
}

View view(uint32_t now_ms) {
    View seen = {};
    seen.placed = state.placed;
    seen.ready = state.ready;
    seen.stale = state.ready && !fresh(now_ms);
    seen.failed = state.failed;
    copy_name(seen.name, sizeof(seen.name), state.name);
    seen.forecast = state.forecast;
    return seen;
}

bool wants_fetch(uint32_t now_ms) { return state.placed && !fresh(now_ms); }

bool coords(float *latitude, float *longitude) {
    if (!state.placed) return false;
    if (latitude) *latitude = state.latitude;
    if (longitude) *longitude = state.longitude;
    return true;
}

}  // namespace weather
