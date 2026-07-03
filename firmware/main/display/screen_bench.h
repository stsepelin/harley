#pragma once
#include "lvgl.h"

// Bench diagnostics screen (sniffer builds only): live RX-pin voltage
// via the ADC, back-calculated bus voltage, line level, edge rate, and
// the J1850 frame counters — the Stage 1/2 checks on the cluster's own
// display instead of a DMM + serial monitor.
lv_obj_t *screen_bench_create(void);
