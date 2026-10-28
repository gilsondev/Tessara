#include "weather/cache.h"
#include "weather/http_contract.h"
#include "weather/parse.h"

#include <unity.h>

#include <cstring>

void test_forecast_url_includes_open_meteo_and_coords(void) {
    char url[400];
    TEST_ASSERT_TRUE(weather::forecast_url(-23.55f, -46.63f, url, sizeof(url)));
    TEST_ASSERT_NOT_NULL(std::strstr(url, "http://api.open-meteo.com/v1/forecast"));
    TEST_ASSERT_NOT_NULL(std::strstr(url, "latitude=-23.55"));
    TEST_ASSERT_NOT_NULL(std::strstr(url, "longitude=-46.63"));
    TEST_ASSERT_NOT_NULL(std::strstr(url, "timezone=auto"));
    TEST_ASSERT_NULL(std::strstr(url, "Sao_Paulo"));
}

void test_accept_http_forecast_rejects_stale_generation(void) {
    weather::reset();
    weather::set_place("SP", -23.55f, -46.63f);
    uint32_t cache_gen = weather::note_request();
    const char *json =
        R"({"current":{"temperature_2m":22.0,"weather_code":1},"daily":{"time":["2026-10-24","2026-10-25","2026-10-26","2026-10-27","2026-10-28","2026-10-29","2026-10-30"],"weather_code":[1,2,3,1,2,3,1],"temperature_2m_max":[25,26,24,25,26,24,25],"temperature_2m_min":[17,18,16,17,18,16,17]}})";

    // Widget generation already bumped (unmount) — must not touch cache.
    TEST_ASSERT_FALSE(weather::accept_http_forecast(/*live*/ 5, /*result*/ 4, cache_gen, json, 1000));
    weather::View v = weather::view(1000);
    TEST_ASSERT_FALSE(v.ready);
}

void test_accept_http_forecast_stores_when_generation_live(void) {
    weather::reset();
    weather::set_place("SP", -23.55f, -46.63f);
    uint32_t cache_gen = weather::note_request();
    const char *json =
        R"({"current":{"temperature_2m":22.0,"weather_code":1},"daily":{"time":["2026-10-24","2026-10-25","2026-10-26","2026-10-27","2026-10-28","2026-10-29","2026-10-30"],"weather_code":[1,2,3,1,2,3,1],"temperature_2m_max":[25,26,24,25,26,24,25],"temperature_2m_min":[17,18,16,17,18,16,17]}})";

    TEST_ASSERT_TRUE(weather::accept_http_forecast(7, 7, cache_gen, json, 1000));
    weather::View v = weather::view(1000);
    TEST_ASSERT_TRUE(v.ready);
    TEST_ASSERT_EQUAL(22, v.forecast.temperature_c);
}

void test_accept_http_forecast_rejects_invalid_body(void) {
    weather::reset();
    weather::set_place("SP", -23.55f, -46.63f);
    uint32_t cache_gen = weather::note_request();
    TEST_ASSERT_FALSE(weather::accept_http_forecast(1, 1, cache_gen, R"({"current":{}})", 1000));
}
