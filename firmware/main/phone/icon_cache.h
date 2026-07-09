#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "phone.h"  // icon_chunk_t

// Cluster-side cache of app-notification icons streamed from the phone as
// PHONE_EVT_ICON chunks (48x48 RGB565, opaque). Chunks are reassembled by
// offset; completed icons live in a small LRU cache keyed by icon_id. The
// notification banner renders one by copying it out (icon_cache_copy).

#define ICON_CACHE_W     48
#define ICON_CACHE_H     48
#define ICON_CACHE_BYTES (ICON_CACHE_W * ICON_CACHE_H * 2)

// Allocate the PSRAM buffers. Call once at boot before feeding.
void icon_cache_init(void);

// Feed one reassembly chunk (BLE host task context).
void icon_cache_feed(const icon_chunk_t *chunk);

// If icon_id is cached, copy its ICON_CACHE_BYTES image into dst and return
// true. Thread-safe against icon_cache_feed (UI task context).
bool icon_cache_copy(uint32_t icon_id, void *dst);
