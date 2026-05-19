#pragma once
// Minimal lvgl.h surface for host tests of phone_data.c, which uses
// lv_tick_get() to timestamp accepted calls. Real LVGL exposes a much
// broader API; this header carries only what the module under test
// actually references.
//
// Tests drive the tick value with lv_tick_stub_set() so the elapsed-time
// branches are deterministic.

#include <stdint.h>

uint32_t lv_tick_get(void);
uint32_t lv_tick_elaps(uint32_t prev_tick);

void     lv_tick_stub_set(uint32_t tick);   // test hook
