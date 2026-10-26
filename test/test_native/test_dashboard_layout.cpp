#include "domain/dashboard_layout.h"

#include <unity.h>

#include <cstring>

void test_dashboard_defaults_are_todoist_weather_aitokens(void) {
    // Name kept for smoke registry; defaults now include hero + Agenda too.
    dashboard::Layout layout = dashboard::defaults();
    TEST_ASSERT_EQUAL(9, layout.count);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_greeting(), layout.ids[0]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_date(), layout.ids[1]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_clock(), layout.ids[2]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_todoist(), layout.ids[3]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_weather(), layout.ids[4]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_aitokens(), layout.ids[5]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_calendar(), layout.ids[6]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_media(), layout.ids[7]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_mac(), layout.ids[8]);
}

void test_dashboard_disable_removes_from_ordered_list(void) {
    dashboard::Layout layout = dashboard::defaults();
    dashboard::set_enabled(layout, dashboard::id_weather(), false);
    TEST_ASSERT_EQUAL(8, layout.count);
    TEST_ASSERT_FALSE(dashboard::is_enabled(layout, dashboard::id_weather()));
    TEST_ASSERT_EQUAL_STRING(dashboard::id_aitokens(), layout.ids[4]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_calendar(), layout.ids[5]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_media(), layout.ids[6]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_mac(), layout.ids[7]);
}

void test_dashboard_enable_appends_when_missing(void) {
    dashboard::Layout layout = dashboard::defaults();
    dashboard::set_enabled(layout, dashboard::id_weather(), false);
    dashboard::set_enabled(layout, dashboard::id_weather(), true);
    TEST_ASSERT_TRUE(dashboard::is_enabled(layout, dashboard::id_weather()));
    TEST_ASSERT_EQUAL(9, layout.count);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_weather(), layout.ids[8]);
}

void test_dashboard_calendar_inserts_after_aitokens(void) {
    dashboard::Layout layout = dashboard::defaults();
    // Calendar already in defaults under AI Tokens.
    TEST_ASSERT_EQUAL(9, layout.count);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_aitokens(), layout.ids[5]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_calendar(), layout.ids[6]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_media(), layout.ids[7]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_mac(), layout.ids[8]);
    // Re-enable when already present but out of order → snap under AI Tokens.
    dashboard::set_enabled(layout, dashboard::id_weather(), false);
    dashboard::set_enabled(layout, dashboard::id_calendar(), false);
    dashboard::set_enabled(layout, dashboard::id_weather(), true);
    dashboard::set_enabled(layout, dashboard::id_calendar(), true);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_aitokens(), layout.ids[4]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_calendar(), layout.ids[5]);
}

void test_dashboard_unknown_ids_ignored_on_parse(void) {
    dashboard::Layout layout = {};
    TEST_ASSERT_TRUE(dashboard::parse("org.tessera.weather,org.fake.nope,org.tessera.todoist", layout));
    TEST_ASSERT_EQUAL(2, layout.count);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_weather(), layout.ids[0]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_todoist(), layout.ids[1]);
}

void test_dashboard_empty_parse_falls_back_to_defaults(void) {
    dashboard::Layout layout = {};
    TEST_ASSERT_TRUE(dashboard::parse("", layout));
    TEST_ASSERT_EQUAL(9, layout.count);
    TEST_ASSERT_TRUE(dashboard::is_enabled(layout, dashboard::id_todoist()));
    TEST_ASSERT_TRUE(dashboard::is_enabled(layout, dashboard::id_greeting()));
    TEST_ASSERT_TRUE(dashboard::is_enabled(layout, dashboard::id_calendar()));
    TEST_ASSERT_TRUE(dashboard::is_enabled(layout, dashboard::id_media()));
    TEST_ASSERT_TRUE(dashboard::is_enabled(layout, dashboard::id_mac()));
}

void test_dashboard_parse_rewrites_legacy_tessara_ids(void) {
    dashboard::Layout layout = {};
    TEST_ASSERT_TRUE(dashboard::parse(
        "org.tessara.todoist,org.tessara.calendar,org.tessara.weather,org.tessera.aitokens", layout));
    TEST_ASSERT_EQUAL(4, layout.count);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_todoist(), layout.ids[0]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_calendar(), layout.ids[1]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_weather(), layout.ids[2]);
    TEST_ASSERT_EQUAL_STRING(dashboard::id_aitokens(), layout.ids[3]);
}

void test_dashboard_ensure_calendar_appends_missing(void) {
    dashboard::Layout layout = {};
    TEST_ASSERT_TRUE(dashboard::parse("org.tessera.todoist,org.tessera.weather,org.tessera.aitokens", layout));
    TEST_ASSERT_FALSE(dashboard::is_enabled(layout, dashboard::id_calendar()));
    TEST_ASSERT_TRUE(dashboard::ensure_calendar(layout));
    TEST_ASSERT_TRUE(dashboard::is_enabled(layout, dashboard::id_calendar()));
    TEST_ASSERT_EQUAL_STRING(dashboard::id_calendar(), layout.ids[3]);  // under AI Tokens
    TEST_ASSERT_FALSE(dashboard::ensure_calendar(layout));
}

void test_dashboard_ensure_media_mac_appends_missing(void) {
    dashboard::Layout layout = {};
    TEST_ASSERT_TRUE(dashboard::parse("org.tessera.todoist,org.tessera.weather,org.tessera.aitokens", layout));
    TEST_ASSERT_FALSE(dashboard::is_enabled(layout, dashboard::id_media()));
    TEST_ASSERT_TRUE(dashboard::ensure_media_mac(layout));
    TEST_ASSERT_TRUE(dashboard::is_enabled(layout, dashboard::id_media()));
    TEST_ASSERT_TRUE(dashboard::is_enabled(layout, dashboard::id_mac()));
    TEST_ASSERT_FALSE(dashboard::ensure_media_mac(layout));  // idempotent
}

void test_dashboard_format_roundtrip(void) {
    dashboard::Layout layout = dashboard::defaults();
    dashboard::set_enabled(layout, dashboard::id_aitokens(), false);
    char csv[256];
    dashboard::format(layout, csv, sizeof(csv));
    dashboard::Layout again = {};
    TEST_ASSERT_TRUE(dashboard::parse(csv, again));
    TEST_ASSERT_EQUAL(layout.count, again.count);
    for (int i = 0; i < layout.count; ++i) {
        TEST_ASSERT_EQUAL_STRING(layout.ids[i], again.ids[i]);
    }
}

void test_dashboard_unknown_set_enabled_is_noop(void) {
    dashboard::Layout layout = dashboard::defaults();
    int before = layout.count;
    dashboard::set_enabled(layout, "org.fake.widget", true);
    TEST_ASSERT_EQUAL(before, layout.count);
}
