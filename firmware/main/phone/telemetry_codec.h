#pragma once
#include "vehicle_data.h"
#include <stddef.h>
#include <stdint.h>

// Cluster -> phone telemetry frame. Same TLV framing as phone_protocol (u8
// type, u16 payload_len LE, payload), on the TX notify characteristic, in a
// type range above the call/media command bytes (0x10..0x30) so a single
// channel stays unambiguous.
//
//   header:   u8  type = 0x40
//             u16 payload_len (LE) = 33
//   payload (all little-endian):
//             u16 speed_raw          raw ECM count (pre-divisor)
//             u16 speed_mph          decoded speed
//             u16 rpm
//             u8  gear               0 = N, 1..6, 7 = unknown
//             i8  engine_temp_c
//             u8  fuel_level         0..6
//             u16 lamps              bitfield, see TELEMETRY_LAMP_* below
//             u32 odometer_m
//             u32 trip1_m
//             u32 trip2_m
//             u32 trip1_fuel_ticks
//             u32 trip2_fuel_ticks
//             u8  clock_hours
//             u8  clock_minutes
//
// The companion app's TelemetryCodec.kt mirrors this byte-for-byte; the host
// test (test_telemetry_codec.c) is the cross-check fixture. Touch one, touch
// both.

#define TELEMETRY_TYPE        0x40u
#define TELEMETRY_PAYLOAD_LEN 33u
#define TELEMETRY_FRAME_LEN   (3u + TELEMETRY_PAYLOAD_LEN)  // 36

#define TELEMETRY_LAMP_TURN_LEFT    (1u << 0)
#define TELEMETRY_LAMP_TURN_RIGHT   (1u << 1)
#define TELEMETRY_LAMP_LOW_BEAM     (1u << 2)
#define TELEMETRY_LAMP_HIGH_BEAM    (1u << 3)
#define TELEMETRY_LAMP_NEUTRAL      (1u << 4)
#define TELEMETRY_LAMP_OIL          (1u << 5)
#define TELEMETRY_LAMP_CHECK_ENGINE (1u << 6)
#define TELEMETRY_LAMP_ABS          (1u << 7)
#define TELEMETRY_LAMP_BATTERY      (1u << 8)
#define TELEMETRY_LAMP_IMMOBILISER  (1u << 9)

// Encode `vd` into `out`. Returns the number of bytes written
// (TELEMETRY_FRAME_LEN) or 0 if the buffer is too small.
size_t telemetry_encode(const vehicle_data_t *vd, uint8_t *out, size_t out_sz);
