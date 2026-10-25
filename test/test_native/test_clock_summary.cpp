#include "clock_summary.h"

#include <unity.h>

#include <cstring>

void test_clock_summary_synced_afternoon(void) {
    clock_face::Summary s = clock_face::from_parts(14, 30, 5, 24, 9, 2025, true);
    TEST_ASSERT_TRUE(s.synced);
    TEST_ASSERT_EQUAL_STRING("14:30", s.time);
    TEST_ASSERT_EQUAL_STRING("Boa tarde", s.greeting);
    TEST_ASSERT_EQUAL_STRING("Sex, 24 de out", s.date_short);
    TEST_ASSERT_EQUAL_STRING("Sexta, 24 de outubro", s.date_long);
}

void test_clock_summary_unsynced(void) {
    clock_face::Summary s = clock_face::from_parts(0, 0, 0, 1, 0, 2025, false);
    TEST_ASSERT_FALSE(s.synced);
    TEST_ASSERT_EQUAL_STRING("--:--", s.time);
    TEST_ASSERT_EQUAL_STRING("", s.greeting);
    TEST_ASSERT_EQUAL_STRING("", s.date_short);
}

void test_clock_summary_greeting_bands(void) {
    TEST_ASSERT_EQUAL_STRING("Boa noite", clock_face::from_parts(3, 0, 1, 1, 0, 2025, true).greeting);
    TEST_ASSERT_EQUAL_STRING("Bom dia", clock_face::from_parts(8, 0, 1, 1, 0, 2025, true).greeting);
    TEST_ASSERT_EQUAL_STRING("Boa tarde", clock_face::from_parts(12, 0, 1, 1, 0, 2025, true).greeting);
    TEST_ASSERT_EQUAL_STRING("Boa noite", clock_face::from_parts(18, 0, 1, 1, 0, 2025, true).greeting);
}

void test_clock_summary_hms_and_dmy(void) {
    clock_face::Options opts;
    opts.time = clock_face::TimeFormat::Hms;
    opts.date = clock_face::DateFormat::Dmy;
    opts.second = 7;
    clock_face::Summary s = clock_face::from_parts(9, 5, 2, 3, 0, 2026, true, opts);
    TEST_ASSERT_EQUAL_STRING("09:05:07", s.time);
    TEST_ASSERT_EQUAL_STRING("03/01/2026", s.date_short);
    TEST_ASSERT_EQUAL_STRING("03/01/2026", s.date_long);
}

void test_clock_summary_parse_ids(void) {
    TEST_ASSERT_TRUE(clock_face::parse_time_format("hms") == clock_face::TimeFormat::Hms);
    TEST_ASSERT_TRUE(clock_face::parse_time_format("hm") == clock_face::TimeFormat::Hm);
    TEST_ASSERT_TRUE(clock_face::parse_date_format("dmy") == clock_face::DateFormat::Dmy);
    TEST_ASSERT_TRUE(clock_face::parse_date_format("ymd") == clock_face::DateFormat::Ymd);
    TEST_ASSERT_EQUAL_STRING("hms", clock_face::time_format_id(clock_face::TimeFormat::Hms));
    TEST_ASSERT_EQUAL_STRING("short_pt", clock_face::date_format_id(clock_face::DateFormat::ShortPt));
}
