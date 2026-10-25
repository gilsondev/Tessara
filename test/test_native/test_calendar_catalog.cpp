#include "calendar/catalog.h"

#include <unity.h>

#include <cstdio>
#include <cstring>

void test_calendar_catalog_parse_and_selection(void) {
    calendar::Catalog c = {};
    const char *line =
        "CALENDAR_CATALOG\tv1\t1\tok\tme@x.com\tprimary\tPrincipal\t1A73E8\t1\tme@x.com\twork\tTrabalho\t0F9D58\t0";
    TEST_ASSERT_TRUE(calendar::apply_catalog_line(c, line));
    TEST_ASSERT_TRUE(c.ready);
    TEST_ASSERT_EQUAL_INT(2, c.count);
    TEST_ASSERT_EQUAL_STRING("primary", c.items[0].id);
    TEST_ASSERT_TRUE(c.items[0].selected);
    TEST_ASSERT_FALSE(c.items[1].selected);

    calendar::apply_selection(c, "work");
    TEST_ASSERT_FALSE(c.items[0].selected);
    TEST_ASSERT_TRUE(c.items[1].selected);

    char sel[64];
    calendar::format_selection(c, sel, sizeof(sel));
    TEST_ASSERT_EQUAL_STRING("work", sel);
}

void test_calendar_auth_pending(void) {
    calendar::Catalog c = {};
    TEST_ASSERT_TRUE(calendar::apply_auth_line(c, "CALENDAR_AUTH\tv1\t1\tabra o browser"));
    TEST_ASSERT_TRUE(c.auth_pending);
    TEST_ASSERT_EQUAL_STRING("abra o browser", c.status);
    TEST_ASSERT_TRUE(calendar::apply_auth_line(c, "CALENDAR_AUTH\tv1\t0\tok"));
    TEST_ASSERT_FALSE(c.auth_pending);
}

void test_calendar_catalog_append_and_long_id(void) {
    calendar::Catalog c = {};
    const char *long_id =
        "gilsondev.in_hd1mr01t9rnbridd00gnb6a5rg@group.calendar.google.com";
    char line1[256];
    std::snprintf(line1, sizeof(line1),
                  "CALENDAR_CATALOG\tv1\t1\tok\tme@x.com\t%s\tContas\tD06B64\t1", long_id);
    TEST_ASSERT_TRUE(calendar::apply_catalog_line(c, line1));
    TEST_ASSERT_EQUAL_INT(1, c.count);
    TEST_ASSERT_EQUAL_STRING(long_id, c.items[0].id);

    TEST_ASSERT_TRUE(calendar::apply_catalog_line(
        c, "CALENDAR_CATALOG\tv1\t1\t+\tother@y.com\tother@y.com\tPrincipal\t1A73E8\t0"));
    TEST_ASSERT_EQUAL_INT(2, c.count);
    TEST_ASSERT_EQUAL_STRING("me@x.com", c.items[0].account);
    TEST_ASSERT_EQUAL_STRING("other@y.com", c.items[1].account);
    TEST_ASSERT_EQUAL_STRING(long_id, c.items[0].id);
}
