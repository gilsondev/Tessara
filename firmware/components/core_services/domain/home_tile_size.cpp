#include "domain/home_tile_size.h"

#include <cstring>

namespace home_tile {

Size clamp(int ordinal) {
  if (ordinal < 0) return Size::S;
  if (ordinal > static_cast<int>(Size::XL)) return Size::XL;
  return static_cast<Size>(ordinal);
}

Size parse(const char *value) {
  if (value == nullptr || value[0] == '\0') return Size::M;
  if (std::strcmp(value, "S") == 0 || std::strcmp(value, "s") == 0 || std::strcmp(value, "0") == 0) {
    return Size::S;
  }
  if (std::strcmp(value, "M") == 0 || std::strcmp(value, "m") == 0 || std::strcmp(value, "1") == 0) {
    return Size::M;
  }
  if (std::strcmp(value, "L") == 0 || std::strcmp(value, "l") == 0 || std::strcmp(value, "2") == 0) {
    return Size::L;
  }
  if (std::strcmp(value, "XL") == 0 || std::strcmp(value, "xl") == 0 || std::strcmp(value, "3") == 0) {
    return Size::XL;
  }
  return Size::M;
}

const char *to_string(Size size) {
  switch (size) {
    case Size::S:
      return "S";
    case Size::M:
      return "M";
    case Size::L:
      return "L";
    case Size::XL:
      return "XL";
  }
  return "M";
}

int content_rows(Size size) {
  switch (size) {
    case Size::S:
      return 1;
    case Size::M:
      return 2;
    case Size::L:
      return 3;
    case Size::XL:
      return 4;
  }
  return 2;
}

int body_min_height_px(Size size) {
  // ~28 px per event row + padding.
  const int rows = content_rows(size);
  return 20 + rows * 28;
}

int tile_min_height_px(Size size) {
  return 40 + body_min_height_px(size);
}

}  // namespace home_tile
