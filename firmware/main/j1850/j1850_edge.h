#pragma once
#include "j1850_vpw.h"
#include <stdbool.h>
#include <stdint.h>

// Reconstructs the held level of each VPW pulse from edge TIMING alone —
// no pin read, so it is immune to the glitch-filter delay vs
// gpio_get_level() race that forced the filter off (see
// docs/j1850-toggling-isr-candidate.md).
//
// Levels strictly alternate, so the level is toggled each edge. Absolute
// phase is anchored by the recessive idle: any pulse longer than the
// longest valid symbol (> J1850_VPW_SOF_MAX_US) was passive (LOW), which
// re-establishes phase and self-limits a missed/spurious edge to a single
// frame (CRC rejects that one; the next inter-frame idle re-syncs).

typedef struct {
    int8_t level;  // level held since the last edge; -1 until anchored
} j1850_edge_t;

void j1850_edge_init(j1850_edge_t *e);

// Given the duration of the pulse that just ended, write its held level
// to *active and return true. Returns false while phase is still unknown
// (before the first idle anchor at startup) — the caller drops the pulse.
bool j1850_edge_level(j1850_edge_t *e, uint32_t dur_us, bool *active);
