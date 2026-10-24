#include "ui/palette.h"

#include <unity.h>

void test_light_paper_is_white_and_ink_is_dark(void) {
    palette::set_mode(palette::Mode::Light);
    TEST_ASSERT_EQUAL_HEX32(0xF6F5F0u, palette::paper());
    TEST_ASSERT_EQUAL_HEX32(0x172C2Bu, palette::ink());
    TEST_ASSERT_EQUAL_HEX32(0x526561u, palette::muted());
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, palette::wash());
    TEST_ASSERT_EQUAL_HEX32(0xD5DFDAu, palette::hairline());
}

void test_dark_inverts_paper_and_ink(void) {
    palette::set_mode(palette::Mode::Dark);
    TEST_ASSERT_EQUAL_HEX32(0x101C1Cu, palette::paper());
    TEST_ASSERT_EQUAL_HEX32(0xEDF5EFu, palette::ink());
    TEST_ASSERT_EQUAL_HEX32(0xA9BDB5u, palette::muted());
    TEST_ASSERT_EQUAL_HEX32(0x192B29u, palette::wash());
    TEST_ASSERT_EQUAL_HEX32(0x36504Au, palette::hairline());
}

void test_kit_colors_stay_in_both_modes(void) {
    palette::set_mode(palette::Mode::Light);
    TEST_ASSERT_EQUAL_HEX32(0x147D73u, palette::primary());
    TEST_ASSERT_EQUAL_HEX32(0xBC632Cu, palette::accent());
    palette::set_mode(palette::Mode::Dark);
    TEST_ASSERT_EQUAL_HEX32(0x65D6C3u, palette::primary());
    TEST_ASSERT_EQUAL_HEX32(0xF4B183u, palette::accent());
    TEST_ASSERT_EQUAL_HEX32(palette::accent(), palette::sand());
    TEST_ASSERT_EQUAL_HEX32(palette::primary(), palette::sky());
}

void test_semantic_tokens_follow_the_mode(void) {
    palette::set_mode(palette::Mode::Light);
    TEST_ASSERT_EQUAL_HEX32(palette::paper(), palette::background());
    TEST_ASSERT_EQUAL_HEX32(palette::wash(), palette::surface());
    TEST_ASSERT_EQUAL_HEX32(palette::ink(), palette::text_primary());
    TEST_ASSERT_EQUAL_HEX32(palette::muted(), palette::text_secondary());
    TEST_ASSERT_EQUAL_HEX32(palette::hairline(), palette::border());
    TEST_ASSERT_EQUAL_HEX32(palette::sand(), palette::warning());
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, palette::surface_elevated());
    TEST_ASSERT_EQUAL_HEX32(0xDC2626u, palette::error());
    palette::set_mode(palette::Mode::Dark);
    TEST_ASSERT_EQUAL_HEX32(palette::background(), palette::paper());
    TEST_ASSERT_EQUAL_HEX32(0x234039u, palette::surface_elevated());
    TEST_ASSERT_EQUAL_HEX32(0xDC2626u, palette::error());
}

void test_priority_and_red_stay_in_both_modes(void) {
    palette::set_mode(palette::Mode::Light);
    const uint32_t light_p1 = palette::priority(4);
    const uint32_t light_red = palette::red();
    palette::set_mode(palette::Mode::Dark);
    TEST_ASSERT_EQUAL_HEX32(0xD1453Bu, light_p1);
    TEST_ASSERT_EQUAL_HEX32(light_p1, palette::priority(4));
    TEST_ASSERT_EQUAL_HEX32(0xEB8909u, palette::priority(3));
    TEST_ASSERT_EQUAL_HEX32(0x246FE0u, palette::priority(2));
    TEST_ASSERT_EQUAL_HEX32(0x4A4A4Au, palette::priority(1));
    TEST_ASSERT_EQUAL_HEX32(0xE44332u, light_red);
    TEST_ASSERT_EQUAL_HEX32(light_red, palette::red());
}
