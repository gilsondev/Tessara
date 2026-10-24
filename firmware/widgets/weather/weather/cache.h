#pragma once

#include "weather/parse.h"

#include <stdint.h>

// Last forecast for the saved place. A newer place drops a reply still in flight.
namespace weather {

struct View {
    bool placed;
    bool ready;
    bool stale;
    bool failed;
    char name[48];
    Forecast forecast;
};

void reset();
void set_place(const char *name, float latitude, float longitude);
uint32_t note_request();
bool store(uint32_t generation, const Forecast &forecast, uint32_t now_ms);
void note_failure();
View view(uint32_t now_ms);
bool wants_fetch(uint32_t now_ms);
bool coords(float *latitude, float *longitude);

}  // namespace weather
