#pragma once
#include <stdint.h>

// Single-pole low-pass step. Moves `current` 25 % of the way toward `target`
// each call; if that quarter rounds to zero but the gap is non-zero, takes a
// ±1 nudge instead so the value always converges. Returns the new value.
//
// Used by the tach to drift the cursor smoothly toward the live RPM while
// still snapping to the target when within a few ticks.
int32_t smooth_step(int32_t current, int32_t target);
