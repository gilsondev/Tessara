#include "platform/device_http.h"

#include <unity.h>

#include <cstring>

namespace {

int g_calls = 0;
uint32_t g_last_gen = 0;
tessera::HttpStatus g_last_status = tessera::HttpStatus::Network;

void on_http(void *, uint32_t generation, const tessera::HttpResult &result) {
    ++g_calls;
    g_last_gen = generation;
    g_last_status = result.status;
}

}  // namespace

void test_device_http_take_complete_delivers_callback(void) {
    tessera::DeviceHttp http;
    g_calls = 0;
    int job = http.get("http://api.open-meteo.com/v1/forecast?x=1", 3, on_http, nullptr);
    TEST_ASSERT_TRUE(job >= 0);
    char url[200];
    uint32_t gen = 0;
    TEST_ASSERT_EQUAL(job, http.take_pending(url, sizeof(url), &gen));
    TEST_ASSERT_EQUAL(3, gen);
    TEST_ASSERT_NOT_NULL(std::strstr(url, "open-meteo.com"));
    TEST_ASSERT_TRUE(http.complete(job, tessera::HttpStatus::Ok, "{}", 2));
    TEST_ASSERT_EQUAL(1, g_calls);
    TEST_ASSERT_EQUAL(3, g_last_gen);
    TEST_ASSERT_EQUAL(int(tessera::HttpStatus::Ok), int(g_last_status));
}

void test_device_http_cancel_generation_drops_late_complete(void) {
    tessera::DeviceHttp http;
    g_calls = 0;
    int job = http.get("http://example.test/a", 9, on_http, nullptr);
    TEST_ASSERT_TRUE(job >= 0);
    char url[200];
    uint32_t gen = 0;
    TEST_ASSERT_EQUAL(job, http.take_pending(url, sizeof(url), &gen));
    http.cancel_generation(9);
    TEST_ASSERT_FALSE(http.complete(job, tessera::HttpStatus::Ok, "x", 1));
    TEST_ASSERT_EQUAL(0, g_calls);
}
