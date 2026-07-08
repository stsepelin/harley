#pragma once
#include "vehicle_data.h"
#include <stdint.h>

// Gear from the RPM:speed ratio. The 2009 VRSCF has no gear-position sensor
// (the bus never reports gear - see firmware/docs/ride-1-findings.md), so gear
// is inferred the way plug-and-play indicators do: each gear has a fixed
// engine-rev-per-mph ratio (the drivetrain's overall ratio x wheel size), and
// the live rpm/speed_mph is matched to the nearest one.
//
// Ratios are exact from the spec (5-speed overall: 1st 10.969 ... 5th 4.563);
// only the single rpm-per-mph scale K depends on rear-tyre circumference and
// the (provisional) speed divisor, so it tracks the speed calibration.
//
// Pure + stateless; pass the previous gear for boundary hysteresis. Returns
// GEAR_1..GEAR_5, or GEAR_UNKNOWN when stopped/creeping/clutch-slipping (too
// slow or rpm/speed out of any gear's band). Neutral is a separate signal
// (the neutral switch bit), not derived here.
gear_t gear_calc(uint16_t rpm, uint16_t speed_mph, gear_t prev);
