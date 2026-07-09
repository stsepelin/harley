#include "icon_cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include <string.h>

// Small LRU of completed icons plus one in-progress reassembly buffer, all in
// PSRAM. Chunks are accepted strictly in order starting at offset 0 (the phone
// sends them sequentially before the NOTIF that references them); anything out
// of order restarts the reassembly, so a dropped chunk just means the icon
// isn't cached and the banner falls back to its text tag.
#define CACHE_N 6

typedef struct {
    uint32_t id;
    uint8_t *buf;
    uint32_t seq;  // last-used counter for LRU
    bool     used;
} entry_t;

static entry_t           s_cache[CACHE_N];
static uint8_t          *s_rx;  // in-progress reassembly buffer
static uint32_t          s_rx_id;
static uint32_t          s_rx_next;  // next expected byte offset
static uint32_t          s_seq;
static SemaphoreHandle_t s_mutex;

void icon_cache_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    for (int i = 0; i < CACHE_N; i++) {
        s_cache[i].buf  = heap_caps_malloc(ICON_CACHE_BYTES, MALLOC_CAP_SPIRAM);
        s_cache[i].used = false;
    }
    s_rx = heap_caps_malloc(ICON_CACHE_BYTES, MALLOC_CAP_SPIRAM);
}

// Store the just-completed reassembly into the cache (reuse a slot for the same
// id, else a free slot, else evict the least-recently-used). Mutex held.
static void commit_locked(void)
{
    entry_t *slot = NULL;
    for (int i = 0; i < CACHE_N && !slot; i++)
        if (s_cache[i].used && s_cache[i].id == s_rx_id)
            slot = &s_cache[i];
    for (int i = 0; i < CACHE_N && !slot; i++)
        if (!s_cache[i].used)
            slot = &s_cache[i];
    if (!slot) {
        slot = &s_cache[0];
        for (int i = 1; i < CACHE_N; i++)
            if (s_cache[i].seq < slot->seq)
                slot = &s_cache[i];
    }
    if (!slot->buf)
        return;
    memcpy(slot->buf, s_rx, ICON_CACHE_BYTES);
    slot->id   = s_rx_id;
    slot->used = true;
    slot->seq  = ++s_seq;
}

void icon_cache_feed(const icon_chunk_t *c)
{
    if (!c || !s_rx || c->total_len != ICON_CACHE_BYTES)
        return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return;

    if (c->offset == 0) {
        s_rx_id   = c->icon_id;
        s_rx_next = 0;
    }
    if (c->icon_id == s_rx_id && c->offset == s_rx_next &&
        (uint32_t)c->offset + c->len <= ICON_CACHE_BYTES) {
        memcpy(s_rx + c->offset, c->data, c->len);
        s_rx_next += c->len;
        if (s_rx_next == ICON_CACHE_BYTES)
            commit_locked();
    }

    xSemaphoreGive(s_mutex);
}

bool icon_cache_copy(uint32_t icon_id, void *dst)
{
    if (icon_id == 0 || !dst)
        return false;
    bool found = false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return false;
    for (int i = 0; i < CACHE_N; i++) {
        if (s_cache[i].used && s_cache[i].id == icon_id && s_cache[i].buf) {
            memcpy(dst, s_cache[i].buf, ICON_CACHE_BYTES);
            s_cache[i].seq = ++s_seq;  // LRU bump
            found          = true;
            break;
        }
    }
    xSemaphoreGive(s_mutex);
    return found;
}
