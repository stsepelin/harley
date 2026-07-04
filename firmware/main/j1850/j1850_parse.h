#pragma once
#include "j1850_vpw.h"
#include "vehicle_data.h"
#include <stdbool.h>

// Pure J1850 message decoder: a decoded VPW frame -> vehicle_data fields.
// Decode table is HarleyDroid-derived and bench-confirmed against the
// 2026-07-04 on-bike capture (firmware/docs/captures/). Host-tested.

// Provisional: 48 29 10 02 reads 0 parked, so the km/h-vs-mph divisor is
// unconfirmed — needs a riding capture (Stage 2 ride agenda). Speed
// decode is wired but the scale is TBD; parked frames read 0 either way.
#define J1850_SPEED_DIVISOR 128

// Decode one frame into *vd. Each broadcast is single-purpose, so this
// updates only the field(s) that frame carries and leaves the rest of
// *vd untouched — the caller keeps a running aggregate and pushes it to
// vehicle_data_set() on a cadence. Returns true if the frame was a
// recognised vehicle message; IM keep-alives / ECM diagnostics / bad-CRC
// frames return false and touch nothing.
bool j1850_parse(const j1850_frame_t *f, vehicle_data_t *vd);
