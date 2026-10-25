#include "mac/snapshot.h"

#include <unity.h>

void test_mac_apply_playing(void) {
    mac::View v{};
    TEST_ASSERT_TRUE(
        mac::apply_line(v, "MEDIA\tv1\t1\tplaying\tSong\tArtist\tMusic", 1000));
    TEST_ASSERT_TRUE(v.ready);
    TEST_ASSERT_TRUE(v.media.ok);
    TEST_ASSERT_EQUAL(static_cast<int>(mac::MediaState::Playing), static_cast<int>(v.media.state));
    TEST_ASSERT_EQUAL_STRING("Song", v.media.title);
    TEST_ASSERT_EQUAL_STRING("Artist", v.media.artist);
    TEST_ASSERT_EQUAL_STRING("Music", v.media.app);
}

void test_mac_apply_paused_keeps_title(void) {
    mac::View v{};
    mac::apply_line(v, "MEDIA\tv1\t1\tpaused\tSong\tArtist\tSpotify", 1);
    TEST_ASSERT_EQUAL(static_cast<int>(mac::MediaState::Paused), static_cast<int>(v.media.state));
    TEST_ASSERT_EQUAL_STRING("Song", v.media.title);
}

void test_mac_apply_stopped(void) {
    mac::View v{};
    mac::apply_line(v, "MEDIA\tv1\t1\tstopped\t\t\t", 1);
    TEST_ASSERT_EQUAL(static_cast<int>(mac::MediaState::Stopped), static_cast<int>(v.media.state));
    TEST_ASSERT_EQUAL_STRING("", v.media.title);
}

void test_mac_apply_ok0(void) {
    mac::View v{};
    mac::apply_line(v, "MEDIA\tv1\t0\t\t\t\t", 1);
    TEST_ASSERT_TRUE(v.ready);
    TEST_ASSERT_FALSE(v.media.ok);
    TEST_ASSERT_EQUAL(static_cast<int>(mac::MediaState::Stopped), static_cast<int>(v.media.state));
}

void test_mac_reject_wrong_prefix(void) {
    mac::View v{};
    TEST_ASSERT_FALSE(mac::apply_line(v, "AITOKENS\tv1\t1", 1));
    TEST_ASSERT_FALSE(v.ready);
}

void test_mac_stale_after_45s(void) {
    mac::View v{};
    mac::apply_line(v, "MEDIA\tv1\t1\tplaying\tA\tB\tC", 1000);
    TEST_ASSERT_FALSE(mac::refresh_age(v, 1000 + 44999));
    TEST_ASSERT_FALSE(v.stale);
    TEST_ASSERT_TRUE(mac::refresh_age(v, 1000 + 45000));
    TEST_ASSERT_TRUE(v.stale);
}
