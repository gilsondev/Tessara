#include "services/todoist_bridge.h"

#include <unity.h>

#include <cstdio>
#include <cstring>

void test_todoist_bridge_multilines(void) {
    TEST_ASSERT_FALSE(todoist_bridge::apply_line("TODOIST\tv1\t1\t1\t1\tHoje\ttoday | overdue", 100));
    TEST_ASSERT_FALSE(
        todoist_bridge::apply_line("TODOIST_TASKS\tv1\t0\t1\t42\t4\tComprar pao\thoje", 110));
    TEST_ASSERT_TRUE(todoist_bridge::apply_line("TODOIST_END\tv1\t1\t1\t1", 120));
    const todoist_bridge::Snapshot *snap = todoist_bridge::last();
    TEST_ASSERT_NOT_NULL(snap);
    TEST_ASSERT_TRUE(snap->bridge_ok);
    TEST_ASSERT_TRUE(snap->linked);
    TEST_ASSERT_TRUE(snap->ready);
    TEST_ASSERT_EQUAL_INT(1, snap->view_count);
    TEST_ASSERT_EQUAL_STRING("Hoje", snap->views[0].item.name);
    TEST_ASSERT_EQUAL_INT(1, snap->views[0].count);
    TEST_ASSERT_EQUAL_STRING("42", snap->views[0].tasks[0].id);
    TEST_ASSERT_EQUAL_STRING("Comprar pao", snap->views[0].tasks[0].content);
}

void test_todoist_bridge_unlinked(void) {
    TEST_ASSERT_TRUE(todoist_bridge::apply_line("TODOIST\tv1\t1\t0\t0", 1));
    const todoist_bridge::Snapshot *snap = todoist_bridge::last();
    TEST_ASSERT_NOT_NULL(snap);
    TEST_ASSERT_TRUE(snap->bridge_ok);
    TEST_ASSERT_FALSE(snap->linked);
    TEST_ASSERT_EQUAL_INT(0, snap->view_count);
}

void test_todoist_bridge_close_ack(void) {
    bool ok = false;
    char id[20] = {};
    TEST_ASSERT_TRUE(todoist_bridge::apply_close_ack("TODOIST_CLOSE\tv1\t1\tabc", &ok, id, sizeof(id)));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("abc", id);
}

void test_todoist_bridge_overlap_header_ignored(void) {
    TEST_ASSERT_FALSE(todoist_bridge::apply_line(
        "TODOIST\tv1\t1\t1\t2\tHoje\ttoday | overdue\tTrabalho\t@Trabalho", 200));
    TEST_ASSERT_FALSE(
        todoist_bridge::apply_line("TODOIST_TASKS\tv1\t0\t1\tidA\t4\tA\t", 210));
    // Second catalog must not wipe staging mid-bundle.
    TEST_ASSERT_FALSE(todoist_bridge::apply_line(
        "TODOIST\tv1\t1\t1\t2\tHoje\ttoday | overdue\tTrabalho\t@Trabalho", 220));
    TEST_ASSERT_FALSE(
        todoist_bridge::apply_line("TODOIST_TASKS\tv1\t1\t1\tidB\t4\tB\t", 230));
    TEST_ASSERT_TRUE(todoist_bridge::apply_line("TODOIST_END\tv1\t1\t1\t2", 240));
    const todoist_bridge::Snapshot *snap = todoist_bridge::last();
    TEST_ASSERT_NOT_NULL(snap);
    TEST_ASSERT_EQUAL_INT(2, snap->view_count);
    TEST_ASSERT_TRUE(snap->views[0].tasks_received);
    TEST_ASSERT_EQUAL_INT(1, snap->views[0].count);
    TEST_ASSERT_EQUAL_STRING("idA", snap->views[0].tasks[0].id);
    TEST_ASSERT_TRUE(snap->views[1].tasks_received);
    TEST_ASSERT_EQUAL_INT(1, snap->views[1].count);
    TEST_ASSERT_EQUAL_STRING("idB", snap->views[1].tasks[0].id);
}

void test_todoist_bridge_tasks_received_flag(void) {
    TEST_ASSERT_FALSE(todoist_bridge::apply_line(
        "TODOIST\tv1\t1\t1\t2\tHoje\ttoday | overdue\tTrabalho\t@Trabalho", 300));
    TEST_ASSERT_FALSE(
        todoist_bridge::apply_line("TODOIST_TASKS\tv1\t0\t1\t9\t4\tSoHoje\t", 310));
    // END without Trabalho TASKS — flag stays false so ingest can keep prior cache.
    TEST_ASSERT_TRUE(todoist_bridge::apply_line("TODOIST_END\tv1\t1\t1\t2", 320));
    const todoist_bridge::Snapshot *snap = todoist_bridge::last();
    TEST_ASSERT_NOT_NULL(snap);
    TEST_ASSERT_TRUE(snap->views[0].tasks_received);
    TEST_ASSERT_FALSE(snap->views[1].tasks_received);
    TEST_ASSERT_EQUAL_INT(0, snap->views[1].count);
}

void test_todoist_bridge_tasks_chunks_append(void) {
    TEST_ASSERT_FALSE(todoist_bridge::apply_line(
        "TODOIST\tv1\t1\t1\t1\tTrabalho\t@Trabalho", 400));
    TEST_ASSERT_FALSE(todoist_bridge::apply_line(
        "TODOIST_TASKS\tv1\t0\t2\ta\t1\tA\t\tb\t1\tB\t", 410));
    TEST_ASSERT_FALSE(todoist_bridge::apply_line(
        "TODOIST_TASKS\tv1\t0\t2\tc\t1\tC\t\td\t1\tD\t", 420));
    TEST_ASSERT_TRUE(todoist_bridge::apply_line("TODOIST_END\tv1\t1\t1\t1", 430));
    const todoist_bridge::Snapshot *snap = todoist_bridge::last();
    TEST_ASSERT_NOT_NULL(snap);
    TEST_ASSERT_TRUE(snap->views[0].tasks_received);
    TEST_ASSERT_EQUAL_INT(4, snap->views[0].count);
    TEST_ASSERT_EQUAL_STRING("a", snap->views[0].tasks[0].id);
    TEST_ASSERT_EQUAL_STRING("d", snap->views[0].tasks[3].id);
}

void test_todoist_tasks_keep_a_question_mark_in_the_title(void) {
    TEST_ASSERT_FALSE(todoist_bridge::apply_line(
        "TODOIST\tv1\t1\t1\t1\tTrabalho\t@Trabalho", 100));
    TEST_ASSERT_FALSE(todoist_bridge::apply_line(
        "TODOIST_TASKS\tv1\t0\t1\ta\t1\tLigar pro João?\t", 110));
    TEST_ASSERT_TRUE(todoist_bridge::apply_line("TODOIST_END\tv1\t1\t1\t1", 120));
    const todoist_bridge::Snapshot *snap = todoist_bridge::last();
    TEST_ASSERT_NOT_NULL(snap);
    TEST_ASSERT_EQUAL_STRING("Ligar pro João?", snap->views[0].tasks[0].content);
}

void test_todoist_bridge_full_list_one_line(void) {
    // Mirrors agent packing: all tasks for one view on a single TASKS line (≤60).
    char line[4096];
    size_t at = 0;
    at += static_cast<size_t>(
        snprintf(line + at, sizeof(line) - at, "TODOIST_TASKS\tv1\t0\t16"));
    for (int i = 0; i < 16; ++i) {
        at += static_cast<size_t>(
            snprintf(line + at, sizeof(line) - at, "\tid%d\t1\ttask%d\t", i, i));
        TEST_ASSERT_TRUE(at < sizeof(line));
    }
    TEST_ASSERT_FALSE(todoist_bridge::apply_line(
        "TODOIST\tv1\t1\t1\t1\tTrabalho\t@Trabalho", 500));
    TEST_ASSERT_FALSE(todoist_bridge::apply_line(line, 510));
    TEST_ASSERT_TRUE(todoist_bridge::apply_line("TODOIST_END\tv1\t1\t1\t1", 520));
    const todoist_bridge::Snapshot *snap = todoist_bridge::last();
    TEST_ASSERT_NOT_NULL(snap);
    TEST_ASSERT_TRUE(snap->views[0].tasks_received);
    TEST_ASSERT_EQUAL_INT(16, snap->views[0].count);
    TEST_ASSERT_EQUAL_STRING("id0", snap->views[0].tasks[0].id);
    TEST_ASSERT_EQUAL_STRING("id15", snap->views[0].tasks[15].id);
}

// A bundle whose END was lost on the wire must not wedge every later snapshot.
void test_todoist_bridge_recovers_from_truncated_bundle(void) {
    TEST_ASSERT_FALSE(todoist_bridge::apply_line(
        "TODOIST\tv1\t1\t1\t1\tTrabalho\t@Trabalho", 1000));
    TEST_ASSERT_FALSE(
        todoist_bridge::apply_line("TODOIST_TASKS\tv1\t0\t1\tlost\t1\tLost\t", 1010));
    // END never arrived; next catalog comes much later and must be accepted.
    TEST_ASSERT_FALSE(todoist_bridge::apply_line(
        "TODOIST\tv1\t1\t1\t1\tTrabalho\t@Trabalho", 9000));
    TEST_ASSERT_FALSE(
        todoist_bridge::apply_line("TODOIST_TASKS\tv1\t0\t1\tgood\t1\tGood\t", 9010));
    TEST_ASSERT_TRUE(todoist_bridge::apply_line("TODOIST_END\tv1\t1\t1\t1", 9020));
    const todoist_bridge::Snapshot *snap = todoist_bridge::last();
    TEST_ASSERT_NOT_NULL(snap);
    TEST_ASSERT_EQUAL_INT(1, snap->views[0].count);
    TEST_ASSERT_EQUAL_STRING("good", snap->views[0].tasks[0].id);
}

// Identical bundles repeat on every heartbeat — each must commit the same counts.
void test_todoist_bridge_repeated_bundle_is_idempotent(void) {
    for (uint32_t t = 0; t < 2; ++t) {
        uint32_t base = 20000 + t * 1000;
        TEST_ASSERT_FALSE(todoist_bridge::apply_line(
            "TODOIST\tv1\t1\t1\t1\tTrabalho\t@Trabalho", base));
        TEST_ASSERT_FALSE(todoist_bridge::apply_line(
            "TODOIST_TASKS\tv1\t0\t2\ta\t1\tA\t\tb\t1\tB\t", base + 10));
        TEST_ASSERT_TRUE(todoist_bridge::apply_line("TODOIST_END\tv1\t1\t1\t1", base + 20));
        const todoist_bridge::Snapshot *snap = todoist_bridge::last();
        TEST_ASSERT_NOT_NULL(snap);
        TEST_ASSERT_EQUAL_INT(2, snap->views[0].count);
        TEST_ASSERT_EQUAL_STRING("a", snap->views[0].tasks[0].id);
    }
}

void test_todoist_bridge_empty_list_marks_received(void) {
    TEST_ASSERT_FALSE(todoist_bridge::apply_line(
        "TODOIST\tv1\t1\t1\t1\tCelular\t@Celular", 600));
    TEST_ASSERT_FALSE(todoist_bridge::apply_line("TODOIST_TASKS\tv1\t0\t0", 610));
    TEST_ASSERT_TRUE(todoist_bridge::apply_line("TODOIST_END\tv1\t1\t1\t1", 620));
    const todoist_bridge::Snapshot *snap = todoist_bridge::last();
    TEST_ASSERT_NOT_NULL(snap);
    TEST_ASSERT_TRUE(snap->views[0].tasks_received);
    TEST_ASSERT_EQUAL_INT(0, snap->views[0].count);
}
