#include "clock_summary.h"

#include <unity.h>

#include <cstring>

void test_clock_summary_synced_afternoon(void) {
    clock_face::Summary s = clock_face::from_parts(14, 30, 5, 24, 9, true);
    TEST_ASSERT_TRUE(s.synced);
    TEST_ASSERT_EQUAL_STRING("14:30", s.time);
    TEST_ASSERT_EQUAL_STRING("Boa tarde", s.greeting);
    TEST_ASSERT_EQUAL_STRING("Sex, 24 de out", s.date_short);
    TEST_ASSERT_EQUAL_STRING("Sexta, 24 out", s.date_long);
}

void test_clock_summary_unsynced(void) {
    clock_face::Summary s = clock_face::from_parts(0, 0, 0, 1, 0, false);
    TEST_ASSERT_FALSE(s.synced);
    TEST_ASSERT_EQUAL_STRING("--:--", s.time);
    TEST_ASSERT_EQUAL_STRING("", s.greeting);
    TEST_ASSERT_EQUAL_STRING("", s.date_short);
}

void test_clock_summary_greeting_bands(void) {
    TEST_ASSERT_EQUAL_STRING("Boa noite", clock_face::from_parts(3, 0, 1, 1, 0, true).greeting);
    TEST_ASSERT_EQUAL_STRING("Bom dia", clock_face::from_parts(8, 0, 1, 1, 0, true).greeting);
    TEST_ASSERT_EQUAL_STRING("Boa tarde", clock_face::from_parts(12, 0, 1, 1, 0, true).greeting);
    TEST_ASSERT_EQUAL_STRING("Boa noite", clock_face::from_parts(18, 0, 1, 1, 0, true).greeting);
}
