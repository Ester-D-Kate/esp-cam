#pragma once
#include <cstddef>
constexpr unsigned MALLOC_CAP_DMA = 1, MALLOC_CAP_INTERNAL = 2;
void* heap_caps_malloc(size_t size, unsigned capabilities);
void heap_caps_free(void* buffer);
