#include "calendar/snapshot.h"

#include <unity.h>

#include <cstring>

void test_calendar_parse_two_events(void) {
    calendar::View v = {};
    const char *line =
        "CALENDAR\tv1\t1\t1\t0900\t1000\t0\tReunião\tJusbrasil\t60\t1A73E8\t0\t1400\t1500\t0\tPlanejamento\t\t60\t0F9D58\t1";
    TEST_ASSERT_TRUE(calendar::apply_line(v, line, 1000));
    TEST_ASSERT_TRUE(v.bridge_ok);
    TEST_ASSERT_TRUE(v.linked);
    TEST_ASSERT_TRUE(v.ready);
    TEST_ASSERT_FALSE(v.stale);
    TEST_ASSERT_EQUAL_INT(2, v.event_count);
    TEST_ASSERT_EQUAL_INT(9 * 60, v.events[0].start_min);
    TEST_ASSERT_EQUAL_INT(10 * 60, v.events[0].end_min);
    TEST_ASSERT_EQUAL_STRING("Reunião", v.events[0].title);
    TEST_ASSERT_EQUAL_STRING("Jusbrasil", v.events[0].location);
    TEST_ASSERT_EQUAL_INT(60, v.events[0].duration_min);
    TEST_ASSERT_EQUAL_HEX32(0x1A73E8u, v.events[0].color_rgb);
    TEST_ASSERT_EQUAL_INT(0, v.events[0].day_offset);
    TEST_ASSERT_EQUAL_STRING("Planejamento", v.events[1].title);
    TEST_ASSERT_EQUAL_HEX32(0x0F9D58u, v.events[1].color_rgb);
    TEST_ASSERT_EQUAL_INT(1, v.events[1].day_offset);
}

void test_calendar_all_day_and_empty_list(void) {
    calendar::View v = {};
    TEST_ASSERT_TRUE(calendar::apply_line(v, "CALENDAR\tv1\t1\t1\t0000\t0000\t1\tFeriado\t\t0\tEA4335", 10));
    TEST_ASSERT_EQUAL_INT(1, v.event_count);
    TEST_ASSERT_TRUE(v.events[0].all_day);

    calendar::View empty = {};
    TEST_ASSERT_TRUE(calendar::apply_line(empty, "CALENDAR\tv1\t1\t1", 20));
    TEST_ASSERT_EQUAL_INT(0, empty.event_count);
    TEST_ASSERT_TRUE(empty.linked);
}

void test_calendar_rejects_non_calendar(void) {
    calendar::View v = {};
    TEST_ASSERT_FALSE(calendar::apply_line(v, "AITOKENS\tv1\t1", 1));
    TEST_ASSERT_FALSE(calendar::apply_line(v, "CALENDAR_CATALOG\tv1\t1", 1));
    TEST_ASSERT_FALSE(v.ready);
}

void test_calendar_unlinked_and_stale(void) {
    calendar::View v = {};
    TEST_ASSERT_TRUE(calendar::apply_line(v, "CALENDAR\tv1\t1\t0", 100));
    TEST_ASSERT_TRUE(v.bridge_ok);
    TEST_ASSERT_FALSE(v.linked);
    TEST_ASSERT_FALSE(calendar::refresh_age(v, 100 + calendar::kStaleMs - 1));
    TEST_ASSERT_FALSE(v.stale);
    TEST_ASSERT_TRUE(calendar::refresh_age(v, 100 + calendar::kStaleMs));
    TEST_ASSERT_TRUE(v.stale);
}
