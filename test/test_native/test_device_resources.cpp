#include "platform/device_resources.h"

#include <unity.h>

#include <cstring>

void test_format_bytes_scales(void) {
    char buf[32];
    device_resources::format_bytes(512, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("512 B", buf);
    device_resources::format_bytes(2048, buf, sizeof(buf));
    TEST_ASSERT_TRUE(std::strstr(buf, "KB") != nullptr);
    device_resources::format_bytes(3ull * 1024ull * 1024ull, buf, sizeof(buf));
    TEST_ASSERT_TRUE(std::strstr(buf, "MB") != nullptr);
}

void test_percent_used(void) {
    TEST_ASSERT_EQUAL(0, device_resources::percent_used(100, 0));
    TEST_ASSERT_EQUAL(0, device_resources::percent_used(100, 100));
    TEST_ASSERT_EQUAL(50, device_resources::percent_used(50, 100));
    TEST_ASSERT_EQUAL(100, device_resources::percent_used(0, 80));
}

void test_format_pair(void) {
    char buf[48];
    device_resources::format_pair(1024, 2048, buf, sizeof(buf));
    TEST_ASSERT_TRUE(std::strstr(buf, "/") != nullptr);
}
