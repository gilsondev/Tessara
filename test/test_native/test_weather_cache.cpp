#include "weather/cache.h"

#include <unity.h>

void test_without_a_place_nothing_is_fetched(void) {
    weather::reset();
    TEST_ASSERT_FALSE(weather::wants_fetch(0));
    weather::View seen = weather::view(0);
    TEST_ASSERT_FALSE(seen.placed);
    TEST_ASSERT_FALSE(seen.ready);
}

void test_cache_is_fresh_at_ten_minutes_and_stale_at_thirty_one(void) {
    weather::reset();
    weather::set_place("São Paulo", -23.55f, -46.63f);
    TEST_ASSERT_TRUE(weather::wants_fetch(0));
    weather::Forecast forecast = {};
    forecast.temperature_c = 21;
    forecast.code = 0;
    const uint32_t generation = weather::note_request();
    TEST_ASSERT_TRUE(weather::store(generation, forecast, 0));
    weather::View fresh = weather::view(10 * 60 * 1000);
    TEST_ASSERT_TRUE(fresh.ready);
    TEST_ASSERT_FALSE(fresh.stale);
    TEST_ASSERT_FALSE(weather::wants_fetch(10 * 60 * 1000));
    TEST_ASSERT_EQUAL(21, fresh.forecast.temperature_c);
    weather::View stale = weather::view(31 * 60 * 1000);
    TEST_ASSERT_TRUE(stale.stale);
    TEST_ASSERT_TRUE(weather::wants_fetch(31 * 60 * 1000));
    TEST_ASSERT_EQUAL(21, stale.forecast.temperature_c);
}

void test_an_older_generation_does_not_replace_the_new_city(void) {
    weather::reset();
    weather::set_place("São Paulo", -23.55f, -46.63f);
    const uint32_t first = weather::note_request();
    weather::set_place("Curitiba", -25.43f, -49.27f);
    weather::Forecast forecast = {};
    forecast.temperature_c = 9;
    TEST_ASSERT_FALSE(weather::store(first, forecast, 1000));
    weather::View seen = weather::view(1000);
    TEST_ASSERT_EQUAL_STRING("Curitiba", seen.name);
    TEST_ASSERT_FALSE(seen.ready);
    TEST_ASSERT_EQUAL(0, seen.forecast.temperature_c);
}
