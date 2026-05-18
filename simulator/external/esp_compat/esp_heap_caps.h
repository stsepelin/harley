#pragma once
// Desktop stand-in for ESP-IDF's capabilities-aware heap. Maps every cap to
// regular malloc/free so production code that asks for PSRAM blocks gets a
// plain host allocation.
#include <stdlib.h>

#define MALLOC_CAP_8BIT     0
#define MALLOC_CAP_32BIT    0
#define MALLOC_CAP_DMA      0
#define MALLOC_CAP_SPIRAM   0
#define MALLOC_CAP_INTERNAL 0

static inline void *heap_caps_malloc(size_t size, int caps) {
    (void)caps;
    return malloc(size);
}
static inline void heap_caps_free(void *ptr) { free(ptr); }
