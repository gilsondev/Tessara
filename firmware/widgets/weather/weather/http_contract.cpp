#include "weather/http_contract.h"

#include "weather/cache.h"
#include "weather/parse.h"

#include <cstdio>

bool weather::forecast_url(float latitude, float longitude, char *out, size_t out_len) {
    if (!out || out_len < 64) return false;
    int n = std::snprintf(
        out, out_len,
        "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min&forecast_days=7&timezone=auto",
        latitude, longitude);
    return n > 0 && static_cast<size_t>(n) < out_len;
}

bool weather::accept_http_forecast(uint32_t live_generation, uint32_t result_generation, uint32_t cache_generation,
                                   const char *body, uint32_t now_ms) {
    if (live_generation == 0 || live_generation != result_generation || !body) return false;
    Forecast forecast = {};
    if (!parse_forecast(body, forecast)) return false;
    return store(cache_generation, forecast, now_ms);
}
