#pragma once
#include "j1850_vpw.h"
#include "vehicle_data.h"
#include <stdbool.h>

// Pure J1850 message decoder: a decoded VPW frame -> vehicle_data fields.
// Decode table is HarleyDroid-derived and bench-confirmed against the
// 2026-07-04 on-bike capture (firmware/docs/captures/). Host-tested.

// SPEED — km/h-native on the bus, magnitude provisional.
// Ride 1 (see firmware/docs/ride-1-findings.md) overturned the earlier
// "mph-native" guess: the ECM speed value is KM/H-native, ~117-128 counts per
// km/h. Fitting the logged RPM/speed pairs against the spec's exact gear
// ratios gives ~117 counts/km-h; the stock speedo peak gives ~124; so true
// km/h ~= counts/120. vehicle_data.speed_mph is mph-canonical, so the divisor
// here converts counts -> mph directly: (counts/120 km/h)/1.609 ~= counts/193.
//
// 188 = 117 counts/km-h x 1.609. The gear-ratio fit landed on ~117 counts/km-h
// on BOTH Ride 1 and Ride 2 (independent captures), and a roadside-radar point
// on Ride 2 confirmed it (true 28 km/h reads 28 km/h at 188). See
// ride-2-findings.md. The runtime divisor (settings/NVS, set by the companion
// GPS calibration) overrides this; the ride log records RAW counts (speed_raw=)
// so it stays re-derivable from any capture.
#define J1850_SPEED_DIVISOR 188

// Decode one frame into *vd. Each broadcast is single-purpose, so this
// updates only the field(s) that frame carries and leaves the rest of
// *vd untouched — the caller keeps a running aggregate and pushes it to
// vehicle_data_set() on a cadence. Returns true if the frame was a
// recognised vehicle message; IM keep-alives / ECM diagnostics / bad-CRC
// frames return false and touch nothing.
bool j1850_parse(const j1850_frame_t *f, vehicle_data_t *vd);
