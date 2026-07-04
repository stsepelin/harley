#pragma once
#include "j1850_vpw.h"
#include "vehicle_data.h"
#include <stdbool.h>

// Pure J1850 message decoder: a decoded VPW frame -> vehicle_data fields.
// Decode table is HarleyDroid-derived and bench-confirmed against the
// 2026-07-04 on-bike capture (firmware/docs/captures/). Host-tested.

// SPEED — units settled, magnitude provisional.
// This is a US-market V-Rod: the stock cluster reads MILES, so the ECM's
// bus value is mph-based ("native"). vehicle_data.speed_mph is now
// mph-canonical (the GPS/sim producers convert to mph too), so the decoded
// value is stored raw with no unit conversion here; the DISPLAY layer does
// the mph->km/h conversion when the user selects metric.
//
// Only the DIVISOR MAGNITUDE is still PROVISIONAL — 128 is a guess for
// raw->mph. Confirm on a ride: the sniffer logs the decoded speed ("speed:"
// line) so it compares DIRECTLY to GPS mph across a range (steady 30/50/70).
// If off by a clean factor, correct DIV.
#define J1850_SPEED_DIVISOR 128

// Decode one frame into *vd. Each broadcast is single-purpose, so this
// updates only the field(s) that frame carries and leaves the rest of
// *vd untouched — the caller keeps a running aggregate and pushes it to
// vehicle_data_set() on a cadence. Returns true if the frame was a
// recognised vehicle message; IM keep-alives / ECM diagnostics / bad-CRC
// frames return false and touch nothing.
bool j1850_parse(const j1850_frame_t *f, vehicle_data_t *vd);
