#include "domain/dashboard_layout.h"

#include <unity.h>

#include <cstring>

void test_home_compose_keeps_enabled_order_intersect_registry(void) {
    dashboard::Layout layout = {};
    dashboard::set_enabled(layout, dashboard::id_weather(), true);
    dashboard::set_enabled(layout, dashboard::id_todoist(), true);
    dashboard::set_enabled(layout, dashboard::id_clock(), true);

    const char *registry[] = {dashboard::id_todoist(), dashboard::id_weather(), dashboard::id_aitokens()};
    char out[dashboard::kMaxSlots][dashboard::kIdLen];
    int n = dashboard::compose(layout, registry, 3, out, dashboard::kMaxSlots);

    // clock enabled but not in registry → dropped; order follows layout (weather, todoist).
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_weather(), out[0]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_todoist(), out[1]);
}

void test_home_compose_new_registry_type_appears_without_layout_change(void) {
    // Defaults include greeting/date/clock + tiles; compose follows layout order ∩ registry.
    dashboard::Layout layout = dashboard::defaults();

    const char *before[] = {dashboard::id_todoist(), dashboard::id_weather(), dashboard::id_aitokens()};
    char out1[dashboard::kMaxSlots][dashboard::kIdLen];
    int n1 = dashboard::compose(layout, before, 3, out1, dashboard::kMaxSlots);
    TEST_ASSERT_EQUAL(3, n1);

    const char *after[] = {dashboard::id_todoist(), dashboard::id_weather(), dashboard::id_aitokens(),
                           dashboard::id_clock()};
    char out2[dashboard::kMaxSlots][dashboard::kIdLen];
    int n2 = dashboard::compose(layout, after, 4, out2, dashboard::kMaxSlots);
    TEST_ASSERT_EQUAL(4, n2);
    // clock is before tile widgets in defaults → first among the registered set.
    TEST_ASSERT_EQUAL_STRING(dashboard::id_clock(), out2[0]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_todoist(), out2[1]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_weather(), out2[2]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_aitokens(), out2[3]);
}
