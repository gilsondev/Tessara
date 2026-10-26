#include "mac/focus_snapshot.h"

#include <unity.h>

#include <cstring>

void test_focus_apply_active_list(void) {
    focus::View v{};
    TEST_ASSERT_TRUE(focus::apply_line(
        v, "FOCUS\tv1\t1\ttrabalho\tTrabalho\t2\ttrabalho\tTrabalho\tpessoal\tPessoal", 1000));
    TEST_ASSERT_TRUE(v.ready);
    TEST_ASSERT_TRUE(v.ok);
    TEST_ASSERT_EQUAL_STRING("trabalho", v.active_id);
    TEST_ASSERT_EQUAL_STRING("Trabalho", v.active_name);
    TEST_ASSERT_EQUAL(2, v.mode_count);
    TEST_ASSERT_EQUAL_STRING("pessoal", v.modes[1].id);
}

void test_focus_apply_none(void) {
    focus::View v{};
    TEST_ASSERT_TRUE(focus::apply_line(v, "FOCUS\tv1\t1\t\t\t1\ttrabalho\tTrabalho", 1));
    TEST_ASSERT_TRUE(v.ok);
    TEST_ASSERT_EQUAL_STRING("", v.active_id);
    TEST_ASSERT_EQUAL(1, v.mode_count);
}

void test_focus_apply_ok0(void) {
    focus::View v{};
    TEST_ASSERT_TRUE(focus::apply_line(v, "FOCUS\tv1\t0\t\t\t0", 1));
    TEST_ASSERT_TRUE(v.ready);
    TEST_ASSERT_FALSE(v.ok);
    TEST_ASSERT_EQUAL(0, v.mode_count);
}

void test_focus_reject_wrong_prefix(void) {
    focus::View v{};
    TEST_ASSERT_FALSE(focus::apply_line(v, "MEDIA\tv1\t1\tplaying\tA\tB\tC", 1));
}

void test_focus_stale_after_45s(void) {
    focus::View v{};
    focus::apply_line(v, "FOCUS\tv1\t1\t\t\t0", 1000);
    TEST_ASSERT_FALSE(focus::refresh_age(v, 1000 + 44999));
    TEST_ASSERT_FALSE(v.stale);
    TEST_ASSERT_TRUE(focus::refresh_age(v, 1000 + 45000));
    TEST_ASSERT_TRUE(v.stale);
}
