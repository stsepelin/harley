#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// NMEA 0183 sentence framing + RMC parsing for the NEO-6M / M8N.
// Pure logic — no UART, no FreeRTOS — so the whole wire format is
// host-testable. gps_uart.c owns the ESP-IDF side and feeds bytes in.

// Spec caps a sentence at 82 chars including "$" and CRLF; real
// receivers occasionally run long, so give some slack.
#define NMEA_MAX_SENTENCE 100

typedef struct {
    char   buf[NMEA_MAX_SENTENCE];
    size_t len;
} nmea_framer_t;

void nmea_framer_init(nmea_framer_t *f);

// Push one received byte. Returns true when buf holds a complete
// NUL-terminated sentence ("$...*hh", CR/LF stripped). Garbage between
// sentences and oversized lines are discarded silently — a cold UART
// joins mid-sentence as a matter of course.
bool nmea_framer_push(nmea_framer_t *f, char c);

typedef struct {
    bool     valid;        // RMC status 'A' (fix); 'V' parses but stays false
    int32_t  lat_e7;       // signed 1e-7 deg
    int32_t  lon_e7;       // signed 1e-7 deg
    uint16_t speed_mph;    // rounded from knots (mph-canonical)
    uint16_t heading_deg;  // 0..359, rounded from course-over-ground
    uint32_t time_utc_ms;  // ms since UTC midnight
} nmea_rmc_t;

// Parse one framed sentence. Accepts any talker ($GPRMC, $GNRMC, ...).
// Returns false for non-RMC sentences, checksum mismatches, and
// malformed fields; true otherwise (including a structurally-sound
// no-fix sentence, which comes back zeroed with valid=false).
bool nmea_parse_rmc(const char *sentence, nmea_rmc_t *out);
