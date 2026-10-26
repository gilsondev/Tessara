#include "app/nav.h"
#include "domain/device_settings.h"

#include <unity.h>

#include <cstring>

void test_open_todoist_and_back(void) {
    nav::State state;
    nav::open_todoist(state);
    TEST_ASSERT_EQUAL(int(nav::Screen::Detail), int(state.screen));
    TEST_ASSERT_EQUAL_STRING("org.tessera.todoist", state.widget_id);
    TEST_ASSERT_FALSE(state.wants_fetch);
    nav::back(state);
    TEST_ASSERT_EQUAL(int(nav::Screen::Home), int(state.screen));
    TEST_ASSERT_EQUAL_STRING("", state.widget_id);
    TEST_ASSERT_FALSE(state.wants_fetch);
}

void test_open_clock(void) {
    nav::State state;
    nav::open_clock(state);
    TEST_ASSERT_EQUAL(int(nav::Screen::Detail), int(state.screen));
    TEST_ASSERT_EQUAL_STRING("org.tessera.clock", state.widget_id);
    TEST_ASSERT_FALSE(state.wants_fetch);
}

void test_open_detail_generic(void) {
    nav::State state;
    nav::open_detail(state, "org.tessera.weather");
    TEST_ASSERT_EQUAL(int(nav::Screen::Detail), int(state.screen));
    TEST_ASSERT_EQUAL_STRING("org.tessera.weather", state.widget_id);
}

void test_open_settings_does_not_fetch(void) {
    nav::State state;
    nav::open_settings(state);
    TEST_ASSERT_EQUAL(int(nav::Screen::Settings), int(state.screen));
    TEST_ASSERT_FALSE(state.wants_fetch);
}

void test_settings_tab_leaves_orientation_and_sync(void) {
    DeviceSettings device;
    device.portrait = true;
    device.sync_minutes = 15;
    nav::State state;
    nav::open_settings(state);
    nav::select_tab(state, nav::Tab::Todoist);
    TEST_ASSERT_EQUAL(int(nav::Tab::Todoist), int(state.tab));
    TEST_ASSERT_FALSE(state.wants_fetch);
    TEST_ASSERT_TRUE(device.portrait);
    TEST_ASSERT_EQUAL_UINT8(15, device.sync_minutes);
}
