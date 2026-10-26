#include "domain/home_tile_size.h"

#include <unity.h>

void test_home_tile_size_parse_and_rows(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(home_tile::Size::M), static_cast<int>(home_tile::parse(nullptr)));
    TEST_ASSERT_EQUAL(static_cast<int>(home_tile::Size::M), static_cast<int>(home_tile::parse("")));
    TEST_ASSERT_EQUAL(static_cast<int>(home_tile::Size::S), static_cast<int>(home_tile::parse("S")));
    TEST_ASSERT_EQUAL(static_cast<int>(home_tile::Size::L), static_cast<int>(home_tile::parse("l")));
    TEST_ASSERT_EQUAL(static_cast<int>(home_tile::Size::XL), static_cast<int>(home_tile::parse("3")));
    TEST_ASSERT_EQUAL_STRING("M", home_tile::to_string(home_tile::Size::M));
    TEST_ASSERT_EQUAL_INT(1, home_tile::content_rows(home_tile::Size::S));
    TEST_ASSERT_EQUAL_INT(2, home_tile::content_rows(home_tile::Size::M));
    TEST_ASSERT_EQUAL_INT(3, home_tile::content_rows(home_tile::Size::L));
    TEST_ASSERT_EQUAL_INT(4, home_tile::content_rows(home_tile::Size::XL));
    TEST_ASSERT_TRUE(home_tile::tile_min_height_px(home_tile::Size::XL) >
                     home_tile::tile_min_height_px(home_tile::Size::S));
}
