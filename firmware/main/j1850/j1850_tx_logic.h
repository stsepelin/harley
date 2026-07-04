#pragma once
#include "j1850_vpw.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Pure-logic helpers for the Stage 4 TX driver. No hardware here — the
// RMT/timer glue lives in j1850_tx.c. Host-tested in vrod_pure.
//
// Bus polarity (settled Phase 3, standard VPW): dominant = bus HIGH,
// driven by the high-side PNP; recessive = bus LOW, released. So on the
// TX GPIO, HIGH keys the bus dominant. Holding it dominant jams every
// node — hence the watchdog limit below.

// Longest valid ACTIVE (dominant) symbol is the SOF (~200 us). Past this
// margin, a continuous dominant is a fault: the driver must refuse the
// stream, and the hardware watchdog must force the line recessive (LOW).
#define J1850_TX_DOMINANT_MAX_US 300

// Passive (recessive) gap that must follow a frame before the bus is
// free (EOF, >= 280 us). Replay cadence (seconds) dwarfs this; it only
// matters for back-to-back emission.
#define J1850_TX_EOF_US 280

// Build a transmit frame: copy the header+payload bytes, append the
// CRC-8/SAE-J1850 byte the decoder checks (data[len-1] == crc(data,
// len-1)). Returns false if n is 0, or n+1 would exceed J1850_MAX_FRAME
// or out_cap. On success *out_len = n + 1.
bool j1850_tx_build_frame(const uint8_t *payload, size_t n, uint8_t *out, size_t out_cap,
                          size_t *out_len);

// Watchdog guard (pure): true only if every ACTIVE pulse is within
// J1850_TX_DOMINANT_MAX_US. The encoder never emits an over-long active
// symbol, so a failure means corruption upstream — the driver must not
// key the bus with it.
bool j1850_tx_stream_within_limits(const j1850_pulse_t *pulses, size_t n);

// Total on-air time of a pulse stream (sum of durations), used to size
// the hardware transmit-timeout backstop.
uint32_t j1850_tx_stream_duration_us(const j1850_pulse_t *pulses, size_t n);
