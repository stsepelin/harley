#include "lvgl.h"

static uint32_t s_tick = 0;

uint32_t lv_tick_get(void)             { return s_tick; }
uint32_t lv_tick_elaps(uint32_t prev)  { return s_tick - prev; }
void     lv_tick_stub_set(uint32_t v)  { s_tick = v; }
