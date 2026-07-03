#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// SAE J1850 VPW (10.4 kbps single-wire, 0V passive / ~7V active) symbol
// codec. Pure logic — the sniffer/driver glue feeds it (level, duration)
// pulses measured off the bus GPIO; the encoder emits the same pulse
// stream for Stage 4 TX. Host-testable end to end (encode → decode
// round trips).
//
// VPW encodes each bit in the width of one pulse, alternating bus level:
//   active  ~64µs = 1    active  ~128µs = 0
//   passive ~64µs = 0    passive ~128µs = 1
// A frame is: SOF (active ~200µs), data bytes MSB-first (12 max
// including the CRC byte), then EOD (passive ~200µs). A responder may
// append an in-frame response (IFR) after EOD: one normalization bit
// then IFR bytes, closed by EOF (passive ≥280µs).

// Receiver classification thresholds, µs (SAE J1850 figure 25 nominals).
#define J1850_VPW_SHORT_MAX_US 96   // <=  : short pulse (64µs nominal)
#define J1850_VPW_LONG_MAX_US  163  // <=  : long pulse (128µs nominal)
#define J1850_VPW_SOF_MAX_US                                                                       \
    239  // <=  : SOF (active) / EOD (passive)
         // >   : EOF / idle (passive)

#define J1850_MAX_FRAME 12  // SAE limit, includes the CRC byte

typedef struct {
    uint8_t data[J1850_MAX_FRAME];  // header + payload + CRC byte
    size_t  len;
    uint8_t ifr[J1850_MAX_FRAME];  // in-frame response bytes, if any
    size_t  ifr_len;
    bool    crc_ok;
} j1850_frame_t;

typedef struct {
    uint8_t       state;  // internal
    uint8_t       bits;   // bits accumulated toward the current byte
    uint8_t       shifter;
    j1850_frame_t frame;
} j1850_vpw_rx_t;

void j1850_vpw_rx_init(j1850_vpw_rx_t *rx);

// Feed one bus pulse: the level that just *ended* and how long it held.
// Returns true when a complete frame (with CRC verdict + any IFR) is
// copied to *out. Frames end on EOF-length passive; the glue layer must
// synthesize a long passive pulse when the bus goes idle, since an
// edge-timed measurement never sees the final pulse end on its own.
bool j1850_vpw_rx_pulse(j1850_vpw_rx_t *rx, bool active, uint32_t dur_us, j1850_frame_t *out);

// One TX pulse for the encoder output.
typedef struct {
    bool     active;
    uint16_t dur_us;
} j1850_pulse_t;

// Encode data (caller includes the CRC byte — see j1850_crc) into the
// SOF + bit pulse sequence. Returns the pulse count, or 0 if out_cap is
// too small / len is 0 or over the frame limit. The caller holds the
// bus passive for EOF afterwards; that idle period isn't a pulse.
size_t j1850_vpw_encode(const uint8_t *data, size_t len, j1850_pulse_t *out, size_t out_cap);

// CRC-8/SAE-J1850: poly 0x1D, init 0xFF, xorout 0xFF ("123456789" → 0x4B).
uint8_t j1850_crc(const uint8_t *data, size_t len);
