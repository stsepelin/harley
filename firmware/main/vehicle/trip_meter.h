#pragma once
#include <stdbool.h>
#include <stdint.h>

// Accumulator for a rolling 16-bit bus counter (odometer ticks A8 69 10, fuel
// ticks A8 83 10). Each broadcast carries the ECM's running count, not a delta,
// so we track the previous value and return the elapsed ticks per frame.
//
// Pure + host-tested. The caller scales the returned ticks into its unit
// (0.4 m per odo tick; fuel ticks stay raw until a fill-up calibrates mL/tick).
typedef struct {
    uint16_t last;
    bool     seeded;
} trip_meter_t;

// Ticks since the previous reading. The first reading seeds and returns 0.
// 16-bit wraparound is handled naturally by the unsigned subtraction. A delta
// larger than max_jump (an ECM reset or a dropped-frame gap) reseeds and
// returns 0, so one bogus jump never corrupts the running total.
uint16_t trip_meter_delta(trip_meter_t *m, uint16_t raw, uint16_t max_jump);
