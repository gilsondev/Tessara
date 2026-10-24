#include "domain/dashboard_layout.h"

#include <unity.h>

#include <cstring>

// Mirrors the portal payload: widgets[{id, shown}] → Layout → CSV → Layout.
void test_dashboard_portal_toggles_roundtrip(void) {
    struct Row {
        const char *id;
        bool shown;
    };
    Row rows[] = {
        {dashboard::id_todoist(), true},
        {dashboard::id_weather(), false},
        {dashboard::id_aitokens(), true},
    };

    dashboard::Layout layout = {};
    for (const Row &row : rows) {
        if (row.shown) dashboard::set_enabled(layout, row.id, true);
    }
    TEST_ASSERT_EQUAL(2, layout.count);
    TEST_ASSERT_FALSE(dashboard::is_enabled(layout, dashboard::id_weather()));

    char csv[192];
    dashboard::format(layout, csv, sizeof(csv));
    TEST_ASSERT_NOT_NULL(std::strstr(csv, dashboard::id_todoist()));
    TEST_ASSERT_NULL(std::strstr(csv, dashboard::id_weather()));

    dashboard::Layout again = {};
    TEST_ASSERT_TRUE(dashboard::parse(csv, again));
    TEST_ASSERT_EQUAL(layout.count, again.count);
    TEST_ASSERT_TRUE(dashboard::is_enabled(again, dashboard::id_todoist()));
    TEST_ASSERT_TRUE(dashboard::is_enabled(again, dashboard::id_aitokens()));
    TEST_ASSERT_FALSE(dashboard::is_enabled(again, dashboard::id_weather()));
}

void test_dashboard_portal_requires_at_least_one_shown(void) {
    dashboard::Layout layout = {};
    // All off → empty layout; parse("") restores defaults (portal rejects before save).
    TEST_ASSERT_EQUAL(0, layout.count);
    dashboard::Layout fallback = {};
    TEST_ASSERT_TRUE(dashboard::parse("", fallback));
    TEST_ASSERT_TRUE(fallback.count >= 1);
}
