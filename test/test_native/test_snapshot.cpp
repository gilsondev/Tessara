#include "todoist/snapshot.h"

#include <unity.h>

void test_summary_sums_ready_visible_lists(void) {
    const todoist::ListTally rows[] = {
        {true, true, 3},
        {true, true, 2},
        {false, true, 9},
    };
    const todoist::Summary summary = todoist::summarize(rows, 3);
    TEST_ASSERT_EQUAL(5, summary.total);
    TEST_ASSERT_FALSE(summary.pending);
}

void test_summary_pending_when_a_visible_list_has_no_cache(void) {
    const todoist::ListTally rows[] = {
        {true, true, 3},
        {true, false, 0},
    };
    const todoist::Summary summary = todoist::summarize(rows, 2);
    TEST_ASSERT_EQUAL(3, summary.total);
    TEST_ASSERT_TRUE(summary.pending);
}

void test_home_count_blank_while_any_list_loads(void) {
    todoist::Summary summary = {};
    summary.total = 3;
    summary.pending = true;
    char text[16] = "x";
    todoist::format_home_count(summary, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("", text);

    summary.total = 0;
    text[0] = 'x';
    todoist::format_home_count(summary, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("", text);
}

void test_home_count_shows_total_when_all_ready(void) {
    todoist::Summary summary = {};
    summary.total = 26;
    summary.pending = false;
    char text[16];
    todoist::format_home_count(summary, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("26", text);
}

void test_late_result_is_rejected_after_leaving_todoist(void) {
    todoist::Gate gate;
    const uint32_t sent = todoist::stamp(gate);
    todoist::bump(gate);
    TEST_ASSERT_FALSE(todoist::accept(gate, sent));
    TEST_ASSERT_TRUE(todoist::accept(gate, todoist::stamp(gate)));
}
