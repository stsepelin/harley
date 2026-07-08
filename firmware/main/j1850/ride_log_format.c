#include "ride_log_format.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// snprintf accumulator: append at offset `pos`, never writing past out_sz, and
// return the running length (which may exceed out_sz so the caller can detect
// truncation). One clamp point so branch coverage stays honest.
static int vappend(char *out, size_t out_sz, int pos, const char *fmt, ...)
{
    size_t  off = (size_t)pos < out_sz ? (size_t)pos : out_sz;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + off, out_sz - off, fmt, ap);
    va_end(ap);
    return pos + n;
}

#define APPEND(...) (pos = vappend(out, out_sz, pos, __VA_ARGS__))

static int append_decode(const j1850_frame_t *f, char *out, size_t out_sz, int pos)
{
    static const uint8_t SPEED[] = {0x48, 0x29, 0x10, 0x02};
    static const uint8_t TEMP[]  = {0xA8, 0x49, 0x10, 0x10};
    static const uint8_t LOAD[]  = {0xA8, 0x3B, 0x10, 0x03};

    if (f->len >= 7 && memcmp(f->data, SPEED, 4) == 0) {
        unsigned raw = ((unsigned)f->data[4] << 8) | f->data[5];
        // Log the RAW km/h-native counts (~117-128 per km/h), not a divided
        // value: it's unit-agnostic, so the speed divisor can be re-derived
        // from any capture (e.g. GPS-correlated) without re-riding.
        APPEND(" | speed_raw=%u", raw);
    } else if (f->len >= 6 && memcmp(f->data, TEMP, 4) == 0) {
        APPEND(" | temp=0x%02X", f->data[4]);  // raw byte; C = raw - 40 (ride-1 confirmed)
    } else if (f->len >= 6 && memcmp(f->data, LOAD, 4) == 0) {
        // A8 3B 10 is an engine-load/throttle param (NOT gear - no gear sensor
        // on this bike); log the raw byte for later identification.
        APPEND(" | load=0x%02X", f->data[4]);
    }
    return pos;
}

int ride_log_format_line(const j1850_frame_t *f, uint64_t t_ms, char *out, size_t out_sz)
{
    int pos = 0;
    if (out_sz == 0)
        return 0;

    // "<sec>.<ms> j1850: " — the j1850: tag keeps the line parseable by the
    // existing capture.py / j1850_report.py regexes, and the leading sec.ms
    // fills report.py's optional timestamp group.
    APPEND("%llu.%03llu j1850: ", (unsigned long long)(t_ms / 1000),
           (unsigned long long)(t_ms % 1000));
    for (size_t i = 0; i < f->len; i++) {
        APPEND("%02X ", f->data[i]);
    }
    APPEND("| CRC %s", f->crc_ok ? "OK" : "BAD");
    if (f->ifr_len > 0) {
        APPEND(" | IFR");
        for (size_t i = 0; i < f->ifr_len; i++) {
            APPEND(" %02X", f->ifr[i]);
        }
    }
    // Decode only trustworthy frames; a bad-CRC line still records raw bytes.
    if (f->crc_ok) {
        pos = append_decode(f, out, out_sz, pos);
    }
    return pos;
}

int ride_log_format_header(uint32_t boot_id, uint64_t t_ms, char *out, size_t out_sz)
{
    int pos = 0;
    if (out_sz == 0)
        return 0;
    APPEND("# session boot=%lu t=%llu.%03llu", (unsigned long)boot_id,
           (unsigned long long)(t_ms / 1000), (unsigned long long)(t_ms % 1000));
    return pos;
}
