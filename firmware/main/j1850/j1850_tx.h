#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Stage 4 J1850 VPW transmitter. Standard VPW / high-side driver: TX GPIO
// HIGH keys the bus DOMINANT (Q1 -> Q2 sources ~7V); LOW releases it to
// RECESSIVE (idle). Holding it dominant jams every node, so an
// independent watchdog forces the line LOW if a dominant persists past
// the longest valid symbol. Compiled only for CONFIG_VROD_J1850_TX;
// read-only sniff builds carry no TX path. See the master plan Stage 4 +
// docs/schematics/j1850_tx.svg.

void j1850_tx_init(void);

// Key one frame onto the bus: payload WITHOUT the CRC byte (the driver
// appends it). Blocks until the RMT transmit completes. Returns false if
// the frame is rejected (bad size / would exceed the dominant-time
// limit) or the watchdog has latched a fault — see j1850_tx_faulted().
bool j1850_tx_send(const uint8_t *payload, size_t n);

// True once the watchdog forced the bus recessive after a stuck dominant.
// Latching: TX stays disabled until j1850_tx_reset() (or a re-init).
bool j1850_tx_faulted(void);

// Clear a latched fault and re-arm TX without a reboot. A watchdog trip
// leaves the pad on the GPIO peripheral (driven LOW); this re-binds it to
// RMT. The bus stays recessive throughout. For the Stage 4 replay: call
// this to resume keep-alives after a transient fault. Returns false if TX
// was never initialised or the re-bind failed.
bool j1850_tx_reset(void);

#if CONFIG_VROD_J1850_TX_SELFTEST
// Bench self-sniff validation loop: emit the IM keep-alive set, let the
// (unchanged) RX sniffer decode it off the same bus node, and log
// per-frame PASS/FAIL + bytes + CRC. Requires CONFIG_VROD_J1850_SNIFFER.
void j1850_tx_selftest_start(void);
#endif
