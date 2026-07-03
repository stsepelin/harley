#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Phase 3 Stage 2 passive capture: GPIO edge timing → VPW decoder →
// one serial log line per bus frame. Read-only by design — the build
// carries no TX path at all, so it cannot disturb the bus regardless
// of what the firmware does. Enabled by CONFIG_VROD_J1850_SNIFFER.
void j1850_sniffer_start(void);

// Snapshot for the bench screen — same numbers the serial stats line
// prints, plus the most recent frame and the live pin level.
typedef struct {
    uint32_t frames;
    uint32_t crc_bad;
    uint32_t overruns;
    uint32_t edges_last_period;  // raw edge count over the last stats window
    uint8_t  last_frame[12];
    size_t   last_len;  // 0 = no frame seen yet
    bool     last_crc_ok;
    bool     pin_level;  // live gpio_get_level of the RX pin
} j1850_sniffer_stats_t;

void j1850_sniffer_get_stats(j1850_sniffer_stats_t *out);

// One-shot ADC read of the RX pin, in millivolts, for the bench screen
// (GPIO 20 sits on ADC1). Returns -1 when the configured pin has no ADC
// channel or calibration isn't available. The pad is flipped to analog
// for the read and restored to digital input + edge IRQ before
// returning. (Linking esp_adc used to crash-loop this board — root
// cause was near-zero pre-scheduler heap margin on rev<v3 silicon,
// fixed via the SDIO queue sizing in sdkconfig.defaults; the story
// lives in docs/ble-bringup-bisect.md.)
int j1850_sniffer_sample_pin_mv(void);
