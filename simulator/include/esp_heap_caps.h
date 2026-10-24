#pragma once

#include <cstdlib>
#include <cstdint>

#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0

inline void *heap_caps_malloc(size_t size, uint32_t) { return std::malloc(size); }
inline void *heap_caps_aligned_alloc(size_t, size_t size, uint32_t) { return std::malloc(size); }
inline void heap_caps_free(void *p) { std::free(p); }
