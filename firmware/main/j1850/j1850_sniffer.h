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

    // Glitch-window auto-sweep (CONFIG_VROD_J1850_GLITCH_SWEEP only);
    // sweep_active stays false in normal builds.
    bool     sweep_active;
    uint32_t sweep_ns;           // window currently applied
    uint32_t sweep_pass;         // full-cycle count so far
    int64_t  sweep_deadline_us;  // esp_timer time this window ends
} j1850_sniffer_stats_t;

void j1850_sniffer_get_stats(j1850_sniffer_stats_t *out);

// Bus-amplitude ADC reads live in j1850_adc_probe.c on a DEDICATED pin
// (GPIO 22) — the sniffer pin can't be time-shared between the digital
// edge ISR and the ADC (the shared read came back garbage). The sniffer
// stays purely digital.
