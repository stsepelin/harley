#pragma once
#include <stdint.h>
#include <stdbool.h>

// Pure-math helpers used by the synthetic driving cycle. Extracted so each
// piece can be unit-tested on a host build without FreeRTOS or the LVGL
// frame loop in the picture.

// Distance travelled during one tick at the given road speed, in metres.
// Converts km/h to m/s and multiplies by the tick. The sim integrates this
// into the odometer and trip counters each tick.
float integrate_distance_m(float speed_kmh, float tick_s);

// Advance a seconds-of-day counter by `delta_s`, wrapping into [0, wrap_s).
// Single-step subtraction wrap — the caller is expected not to overshoot by
// more than one full wrap per call (true for our 0.05 s ticks vs 24 h day).
float clock_advance(float seconds, float delta_s, float wrap_s);

// Split a seconds-of-day value into HH:MM (truncated, no rounding).
// Negative or wrap-overflow inputs are clamped to a well-defined range.
void clock_seconds_to_hm(float seconds, uint8_t *out_h, uint8_t *out_m);

// Fuel cycle state machine: every `step_s` of accumulated `tick_s` the
// level drops by one; from 0 it wraps back up to `max_level` (refuel).
// Returns true on ticks where the level actually changed.
bool fuel_tick(float *progress, uint8_t *level,
               float tick_s, float step_s, uint8_t max_level);
