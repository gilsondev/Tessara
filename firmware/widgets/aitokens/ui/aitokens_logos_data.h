#pragma once

#include <stddef.h>
#include <stdint.h>

// Simple Icons (CC0) product marks, RGB565A8.
namespace aitokens_logos_data {
int side();
const uint8_t *codex(size_t *bytes);
const uint8_t *claude(size_t *bytes);
const uint8_t *cursor(size_t *bytes);
}
