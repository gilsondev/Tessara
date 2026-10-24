#pragma once

#include <stddef.h>
#include <stdint.h>

// Host-testable weather HTTP contract (WIDGET-SDK stale-result rule).
namespace weather {

// Full URL for Open-Meteo forecast (plain HTTP — same as weather_client).
bool forecast_url(float latitude, float longitude, char *out, size_t out_len);

// Apply a forecast HTTP body only when the widget generation is still live.
// `cache_generation` is the tag from note_request() for this in-flight fetch.
bool accept_http_forecast(uint32_t live_generation, uint32_t result_generation, uint32_t cache_generation,
                          const char *body, uint32_t now_ms);

}  // namespace weather
