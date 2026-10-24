#include "platform/screen_rest.h"

#include <unity.h>

void test_screen_rest_enters_saver_after_idle(void) {
    screen_rest::State state;
    screen_rest::begin(state, 0, 80);
    TEST_ASSERT_FALSE(screen_rest::tick(state, screen_rest::kIdleBeforeSaverMs - 1, true));
    TEST_ASSERT_EQUAL(screen_rest::Phase::Awake, state.phase);
    TEST_ASSERT_TRUE(screen_rest::tick(state, screen_rest::kIdleBeforeSaverMs, true));
    TEST_ASSERT_EQUAL(screen_rest::Phase::Saver, state.phase);
    TEST_ASSERT_EQUAL(80, screen_rest::brightness_now(state, screen_rest::kIdleBeforeSaverMs));
}

void test_screen_rest_saver_then_fade_then_off(void) {
    screen_rest::State state;
    screen_rest::begin(state, 0, 100);
    screen_rest::force(state, screen_rest::Phase::Saver, 1000);
    uint32_t t = 1000 + screen_rest::kSaverBeforeFadeMs;
    TEST_ASSERT_TRUE(screen_rest::tick(state, t, true));
    TEST_ASSERT_EQUAL(screen_rest::Phase::Fading, state.phase);
    TEST_ASSERT_EQUAL(100, screen_rest::brightness_now(state, t));
    TEST_ASSERT_EQUAL(50, screen_rest::brightness_now(state, t + screen_rest::kFadeMs / 2));
    TEST_ASSERT_TRUE(screen_rest::tick(state, t + screen_rest::kFadeMs, true));
    TEST_ASSERT_EQUAL(screen_rest::Phase::Off, state.phase);
    TEST_ASSERT_EQUAL(0, screen_rest::brightness_now(state, t + screen_rest::kFadeMs));
}

void test_screen_rest_activity_wakes_and_marks_swallow(void) {
    screen_rest::State state;
    screen_rest::begin(state, 0, 100);
    screen_rest::force(state, screen_rest::Phase::Off, 5000);
    screen_rest::note_activity(state, 6000);
    TEST_ASSERT_EQUAL(screen_rest::Phase::Awake, state.phase);
    TEST_ASSERT_TRUE(state.wake_touch_pending);
    TEST_ASSERT_TRUE(screen_rest::consume_wake_touch(state));
    TEST_ASSERT_FALSE(screen_rest::consume_wake_touch(state));
}

void test_screen_rest_blocked_while_not_allowed(void) {
    screen_rest::State state;
    screen_rest::begin(state, 0, 100);
    screen_rest::force(state, screen_rest::Phase::Saver, 100);
    TEST_ASSERT_TRUE(screen_rest::tick(state, 200, false));
    TEST_ASSERT_EQUAL(screen_rest::Phase::Awake, state.phase);
}

void test_screen_rest_never_stays_awake(void) {
    screen_rest::State state;
    screen_rest::begin(state, 0, 100);
    screen_rest::set_idle_minutes(state, 0);
    TEST_ASSERT_FALSE(screen_rest::tick(state, 60u * 60u * 1000u, true));
    TEST_ASSERT_EQUAL(screen_rest::Phase::Awake, state.phase);
}

void test_screen_rest_idle_minutes_two(void) {
    screen_rest::State state;
    screen_rest::begin(state, 0, 100);
    screen_rest::set_idle_minutes(state, 2);
    TEST_ASSERT_FALSE(screen_rest::tick(state, 2u * 60u * 1000u - 1, true));
    TEST_ASSERT_TRUE(screen_rest::tick(state, 2u * 60u * 1000u, true));
    TEST_ASSERT_EQUAL(screen_rest::Phase::Saver, state.phase);
}

void test_screen_rest_clock_minutes_two(void) {
    screen_rest::State state;
    screen_rest::begin(state, 0, 100);
    screen_rest::set_clock_minutes(state, 2);
    screen_rest::force(state, screen_rest::Phase::Saver, 1000);
    TEST_ASSERT_FALSE(screen_rest::tick(state, 1000 + 2u * 60u * 1000u - 1, true));
    TEST_ASSERT_EQUAL(screen_rest::Phase::Saver, state.phase);
    TEST_ASSERT_TRUE(screen_rest::tick(state, 1000 + 2u * 60u * 1000u, true));
    TEST_ASSERT_EQUAL(screen_rest::Phase::Fading, state.phase);
}

void test_screen_rest_clock_until_touch(void) {
    screen_rest::State state;
    screen_rest::begin(state, 0, 100);
    screen_rest::set_clock_minutes(state, 0);
    screen_rest::force(state, screen_rest::Phase::Saver, 1000);
    TEST_ASSERT_FALSE(screen_rest::tick(state, 1000 + 60u * 60u * 1000u, true));
    TEST_ASSERT_EQUAL(screen_rest::Phase::Saver, state.phase);
}
