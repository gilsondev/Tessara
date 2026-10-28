#pragma once

#include "weather/parse.h"

#include <stddef.h>
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
// Same as store, but the saved forecast is dated at epoch_s. The device passes the wall clock.
bool store_at(uint32_t generation, const Forecast &forecast, uint32_t now_ms, int64_t epoch_s);
using BlobLoad = bool (*)(char *out, size_t out_len);
using BlobSave = bool (*)(const char *blob);
// Where the last forecast lives across a reboot. Null restores the in-process buffer.
void set_blob_io(BlobLoad load, BlobSave save);
void note_failure();
View view(uint32_t now_ms);
bool wants_fetch(uint32_t now_ms);
// Civil date the panel is using, YYYY-MM-DD. Empty means the clock is not set.
void note_today(const char *yyyy_mm_dd);
// Local hour, 0–23. A change releases a date refetch that came back on the wrong day.
void note_hour(int hour);
const char *today();
// Index of the row dated `today`, or -1 when that date is not in the forecast.
int day_offset(const Forecast &forecast, const char *today);
// Backoff after a failed forecast. n=0 is 30 s; n>=6 stays at 30 min.
uint32_t next_retry_ms(uint32_t failures);
// A cleared backoff stays open after millis() passes 2^31. A set backoff waits until next_ms.
bool retry_allowed(uint32_t now_ms, bool backoff, uint32_t next_ms);
bool coords(float *latitude, float *longitude);

}  // namespace weather
