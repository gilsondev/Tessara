#include "weather/codes.h"
#include "weather/parse.h"

#include <unity.h>

void test_phrase_maps_known_codes_and_falls_back(void) {
    TEST_ASSERT_EQUAL_STRING("céu limpo", weather::phrase(0));
    TEST_ASSERT_EQUAL_STRING("chuva", weather::phrase(61));
    TEST_ASSERT_EQUAL_STRING("tempo", weather::phrase(999));
}

void test_parse_place_reads_the_first_result(void) {
    const char *json =
        R"({"results":[{"name":"São Paulo","latitude":-23.55,"longitude":-46.63},{"name":"Outra","latitude":0,"longitude":0}]})";
    weather::Place place = {};
    TEST_ASSERT_TRUE(weather::parse_place(json, place));
    TEST_ASSERT_EQUAL_STRING("São Paulo", place.name);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -23.55f, place.latitude);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -46.63f, place.longitude);
}

void test_parse_place_rejects_an_empty_result_without_writing(void) {
    weather::Place place = {};
    place.latitude = 7;
    TEST_ASSERT_FALSE(weather::parse_place(R"({"results":[]})", place));
    TEST_ASSERT_EQUAL_STRING("", place.name);
    TEST_ASSERT_EQUAL_FLOAT(7, place.latitude);
}

void test_parse_forecast_reads_now_and_seven_days(void) {
    const char *json =
        R"({"current":{"temperature_2m":21,"weather_code":61},"daily":{"time":["2026-10-23","2026-10-24","2026-10-25","2026-10-26","2026-10-27","2026-10-28","2026-10-29"],"weather_code":[61,3,0,1,2,61,95],"temperature_2m_max":[24,22,26,25,23,21,20],"temperature_2m_min":[16,15,14,15,16,17,18]}})";
    weather::Forecast forecast = {};
    TEST_ASSERT_TRUE(weather::parse_forecast(json, forecast));
    TEST_ASSERT_EQUAL(21, forecast.temperature_c);
    TEST_ASSERT_EQUAL(61, forecast.code);
    TEST_ASSERT_EQUAL_STRING("chuva", weather::phrase(forecast.code));
    TEST_ASSERT_EQUAL(24, forecast.days[0].temperature_max_c);
    TEST_ASSERT_EQUAL(16, forecast.days[0].temperature_min_c);
    TEST_ASSERT_EQUAL(61, forecast.days[0].code);
    TEST_ASSERT_EQUAL_STRING("2026-10-23", forecast.days[0].date);
    TEST_ASSERT_EQUAL(95, forecast.days[6].code);
    TEST_ASSERT_EQUAL(20, forecast.days[6].temperature_max_c);
}

void test_parse_forecast_missing_field_leaves_the_struct(void) {
    weather::Forecast forecast = {};
    forecast.temperature_c = 7;
    TEST_ASSERT_FALSE(weather::parse_forecast(R"({"current":{"weather_code":61}})", forecast));
    TEST_ASSERT_EQUAL(7, forecast.temperature_c);
}
