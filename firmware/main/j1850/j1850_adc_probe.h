#pragma once
#include <stdbool.h>

// Bus-amplitude probe (Phase 3 Stage 4 prep). Reads node B via the ADC
// on a DEDICATED GPIO (CONFIG_VROD_J1850_ADC_GPIO, second wire off node
// B) — never the sniffer's digital GPIO 20, which can't be time-shared
// with the edge ISR + glitch filter. Tracks min/max over ~2 s windows,
// applies the 10k/4.7k divider factor (x147/47), logs the bus idle/low
// levels. On this active-low bus, MAX = the idle resting (HIGH) level —
// the number the Stage 4 TX driver has to overcome.
void j1850_adc_probe_start(void);

// Latest window's node-B min/max in mV. false if the probe isn't
// enabled or hasn't produced a window yet. Multiply by 147/47 for bus V.
bool j1850_adc_probe_get(int *max_mv, int *min_mv);
