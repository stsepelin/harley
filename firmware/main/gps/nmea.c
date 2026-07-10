#include "nmea.h"
#include <string.h>

void nmea_framer_init(nmea_framer_t *f)
{
    f->len = 0;
}

bool nmea_framer_push(nmea_framer_t *f, char c)
{
    if (c == '$') {  // sentence start — always resync
        f->buf[0] = '$';
        f->len    = 1;
        return false;
    }
    if (f->len == 0)
        return false;  // between sentences: drop noise
    if (c == '\r' || c == '\n') {
        f->buf[f->len] = '\0';
        f->len         = 0;
        return true;
    }
    if (f->len >= NMEA_MAX_SENTENCE - 1) {  // oversized: not a sentence
        f->len = 0;
        return false;
    }
    f->buf[f->len++] = c;
    return false;
}

// --- field helpers ---------------------------------------------------

static bool hex_nibble(char c, uint8_t *out)
{
    if (c >= '0' && c <= '9') {
        *out = (uint8_t)(c - '0');
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *out = (uint8_t)(c - 'A' + 10);
        return true;
    }
    return false;
}

// Parse "digits[.digits]" as a value scaled by 10^frac_digits, so
// "12.5" with frac_digits=3 → 12500. Extra fraction digits truncate.
// Rejects empty strings and any non-digit character.
static bool parse_fixed(const char *s, size_t len, int frac_digits, int64_t *out)
{
    if (len == 0)
        return false;
    int64_t v      = 0;
    size_t  i      = 0;
    bool    dotted = false;
    int     fplace = 0;
    for (; i < len; i++) {
        char c = s[i];
        if (c == '.') {
            if (dotted || i == 0)
                return false;  // ".5" / "1..2"
            dotted = true;
            continue;
        }
        if (c < '0' || c > '9')
            return false;
        if (dotted) {
            if (fplace >= frac_digits)
                continue;  // truncate extras
            fplace++;
        }
        v = v * 10 + (c - '0');
    }
    while (fplace < frac_digits) {
        v *= 10;
        fplace++;
    }
    *out = v;
    return true;
}

// "ddmm.mmmm" (lat, deg_digits=2) / "dddmm.mmmm" (lon, deg_digits=3)
// → signed 1e-7 degrees. Minutes are carried at 1e-5 precision, then
// rounded into 1e-7 deg: min/60 * 1e7 = min_1e5 * 10 / 6.
static bool parse_coord(const char *s, size_t len, int deg_digits, char hemi, char pos, char neg,
                        int32_t *out)
{
    const char *dot     = memchr(s, '.', len);
    size_t      int_len = dot ? (size_t)(dot - s) : len;
    if (int_len != (size_t)(deg_digits + 2))
        return false;

    int32_t deg = 0;
    for (int i = 0; i < deg_digits; i++) {
        char c = s[i];
        if (c < '0' || c > '9')
            return false;
        deg = deg * 10 + (c - '0');
    }
    int64_t min_1e5;
    if (!parse_fixed(s + deg_digits, len - (size_t)deg_digits, 5, &min_1e5))
        return false;
    if (min_1e5 >= 60 * 100000LL)
        return false;  // minutes must be < 60

    int64_t e7 = (int64_t)deg * 10000000LL + (min_1e5 * 10 + 3) / 6;
    if (hemi == neg)
        e7 = -e7;
    else if (hemi != pos)
        return false;
    *out = (int32_t)e7;
    return true;
}

// "hhmmss[.sss]" → ms since UTC midnight.
static bool parse_time(const char *s, size_t len, uint32_t *out)
{
    int64_t t_1e3;  // hhmmss scaled by 1e3 → the .sss lands as ms
    if (!parse_fixed(s, len, 3, &t_1e3))
        return false;
    const char *dot     = memchr(s, '.', len);
    size_t      int_len = dot ? (size_t)(dot - s) : len;
    if (int_len != 6)
        return false;

    uint32_t ms = (uint32_t)(t_1e3 % 1000);
    uint32_t ss = (uint32_t)(t_1e3 / 1000) % 100;
    uint32_t mm = (uint32_t)(t_1e3 / 100000) % 100;
    uint32_t hh = (uint32_t)(t_1e3 / 10000000);
    if (hh > 23 || mm > 59 || ss > 59)
        return false;
    *out = ((hh * 60 + mm) * 60 + ss) * 1000 + ms;
    return true;
}

// --- RMC -------------------------------------------------------------

#define RMC_MIN_FIELDS 10  // type..date; receivers append more, fine

typedef struct {
    const char *s;
    size_t      len;
} field_t;

bool nmea_parse_rmc(const char *sentence, nmea_rmc_t *out)
{
    memset(out, 0, sizeof(*out));
    if (sentence[0] != '$')
        return false;

    // Checksum: XOR of everything between '$' and '*', then two hex
    // digits and nothing else (the framer already ate CR/LF).
    uint8_t     sum = 0;
    const char *p   = sentence + 1;
    for (; *p != '\0' && *p != '*'; p++)
        sum ^= (uint8_t)*p;
    if (*p != '*')
        return false;
    uint8_t hi, lo;
    if (!hex_nibble(p[1], &hi) || !hex_nibble(p[2], &lo) || p[3] != '\0')
        return false;
    if (sum != (uint8_t)((hi << 4) | lo))
        return false;

    // Split the body into fields. Empty fields are legitimate (a cold
    // receiver sends "$GPRMC,,V,,,,,,,,,,N*53").
    field_t     f[RMC_MIN_FIELDS];
    size_t      nf = 0;
    const char *b  = sentence + 1;
    while (nf < RMC_MIN_FIELDS) {
        const char *e = b;
        while (e < p && *e != ',')
            e++;
        f[nf].s   = b;
        f[nf].len = (size_t)(e - b);
        nf++;
        if (e == p)
            break;
        b = e + 1;
    }
    if (nf < RMC_MIN_FIELDS)
        return false;

    // Any talker ("GP" NEO-6M, "GN" M8N multi-constellation, ...).
    if (f[0].len != 5 || memcmp(f[0].s + 2, "RMC", 3) != 0)
        return false;

    if (f[2].len != 1)
        return false;
    if (f[2].s[0] == 'V')
        return true;  // no fix: zeroed, valid=false
    if (f[2].s[0] != 'A')
        return false;

    if (!parse_time(f[1].s, f[1].len, &out->time_utc_ms))
        return false;
    if (!parse_coord(f[3].s, f[3].len, 2, f[4].len == 1 ? f[4].s[0] : '?', 'N', 'S', &out->lat_e7))
        return false;
    if (!parse_coord(f[5].s, f[5].len, 3, f[6].len == 1 ? f[6].s[0] : '?', 'E', 'W', &out->lon_e7))
        return false;

    // Speed over ground, knots → mph, rounded. 1 kn = 1852 m/h ÷ 1609.344 m/mi;
    // knots_1e3 × 1852 / 1609344 is that ratio with knots_1e3 = knots × 1000.
    // Speed is mph-canonical (see vehicle_data.h). Empty = 0 (some receivers
    // blank it while stationary).
    if (f[7].len > 0) {
        int64_t knots_1e3;
        if (!parse_fixed(f[7].s, f[7].len, 3, &knots_1e3))
            return false;
        int64_t mph    = (knots_1e3 * 1852 + 804672) / 1609344;
        out->speed_mph = (mph > 65535) ? 65535 : (uint16_t)mph;
    }

    // Course over ground, degrees true. Empty = 0 (blanked when there's
    // no motion to derive it from).
    if (f[8].len > 0) {
        int64_t deg_1e1;
        if (!parse_fixed(f[8].s, f[8].len, 1, &deg_1e1))
            return false;
        uint32_t deg = (uint32_t)((deg_1e1 + 5) / 10);
        if (deg > 360)
            return false;
        out->heading_deg = (uint16_t)(deg % 360);  // "360.0" wraps to 0
    }

    out->valid = true;
    return true;
}
