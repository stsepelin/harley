// NMEA framer + RMC parser. The framer is fed byte streams the way
// gps_uart.c would; the parser gets checksummed sentences built by
// mk() so a fixture edit can't silently rot its checksum, plus the
// classic datasheet RMC example with its published checksum as the
// one hand-verifiable anchor.

#include "unity.h"
#include "nmea.h"
#include <stdio.h>
#include <string.h>

// "$<body>*HH" with the checksum computed the same way the receiver
// does (XOR of the body).
static const char *mk(char *out, size_t cap, const char *body)
{
    uint8_t sum = 0;
    for (const char *p = body; *p; p++)
        sum ^= (uint8_t)*p;
    snprintf(out, cap, "$%s*%02X", body, sum);
    return out;
}

// --- framer ----------------------------------------------------------

static void test_framer_assembles_sentence_and_strips_crlf(void)
{
    nmea_framer_t f;
    nmea_framer_init(&f);
    const char *stream = "$GPRMC,x*00\r\n";
    bool        done   = false;
    for (const char *p = stream; *p; p++) {
        bool r = nmea_framer_push(&f, *p);
        if (r) {
            TEST_ASSERT_FALSE(done);  // must fire exactly once, on CR
            TEST_ASSERT_EQUAL_STRING("$GPRMC,x*00", f.buf);
            done = true;
        }
    }
    TEST_ASSERT_TRUE(done);  // the trailing LF is inter-sentence noise
}

static void test_framer_drops_noise_between_sentences(void)
{
    nmea_framer_t f;
    nmea_framer_init(&f);
    // UART joined mid-stream: tail of a previous sentence, then a real one.
    for (const char *p = "31,E*7\r\n"; *p; p++) {
        TEST_ASSERT_FALSE(nmea_framer_push(&f, *p));
    }
    bool done = false;
    for (const char *p = "$GPGGA,1*00\r"; *p; p++)
        done = nmea_framer_push(&f, *p);
    TEST_ASSERT_TRUE(done);
    TEST_ASSERT_EQUAL_STRING("$GPGGA,1*00", f.buf);
}

static void test_framer_resyncs_on_dollar_mid_sentence(void)
{
    nmea_framer_t f;
    nmea_framer_init(&f);
    for (const char *p = "$GPRMC,garbled$GPGGA,2*00"; *p; p++) {
        TEST_ASSERT_FALSE(nmea_framer_push(&f, *p));
    }
    TEST_ASSERT_TRUE(nmea_framer_push(&f, '\r'));
    TEST_ASSERT_EQUAL_STRING("$GPGGA,2*00", f.buf);
}

static void test_framer_accepts_bare_lf_terminator(void)
{
    // Some receivers (and most logged captures) end lines with plain
    // LF — either line ending must complete the sentence.
    nmea_framer_t f;
    nmea_framer_init(&f);
    bool done = false;
    for (const char *p = "$GPGGA,3*00\n"; *p; p++)
        done = nmea_framer_push(&f, *p);
    TEST_ASSERT_TRUE(done);
    TEST_ASSERT_EQUAL_STRING("$GPGGA,3*00", f.buf);
}

static void test_framer_discards_oversized_lines(void)
{
    nmea_framer_t f;
    nmea_framer_init(&f);
    TEST_ASSERT_FALSE(nmea_framer_push(&f, '$'));
    for (int i = 0; i < NMEA_MAX_SENTENCE + 10; i++) {
        TEST_ASSERT_FALSE(nmea_framer_push(&f, 'A'));
    }
    // The overrun reset means the eventual CR is inter-sentence noise.
    TEST_ASSERT_FALSE(nmea_framer_push(&f, '\r'));
}

// --- parser: the happy paths -----------------------------------------

static void test_parse_datasheet_example(void)
{
    // The canonical RMC example (u-blox / NMEA docs), checksum as
    // published: 48°07.038'N 011°31.000'E, 22.4 kn, course 084.4.
    nmea_rmc_t r;
    TEST_ASSERT_TRUE(
        nmea_parse_rmc("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A", &r));
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_INT32(481173000, r.lat_e7);
    TEST_ASSERT_EQUAL_INT32(115166667, r.lon_e7);
    TEST_ASSERT_EQUAL_UINT16(26, r.speed_mph);  // 22.4 kn × 1.15078 = 25.78
    TEST_ASSERT_EQUAL_UINT16(84, r.heading_deg);
    TEST_ASSERT_EQUAL_UINT32(((12 * 60 + 35) * 60 + 19) * 1000, r.time_utc_ms);
}

static void test_parse_southern_western_hemispheres(void)
{
    char       s[128];
    nmea_rmc_t r;
    mk(s, sizeof(s), "GNRMC,000000.00,A,3357.0000,S,15112.0000,W,0.0,0.0,010126,,,A");
    TEST_ASSERT_TRUE(nmea_parse_rmc(s, &r));
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_INT32(-339500000, r.lat_e7);   // 33°57' S
    TEST_ASSERT_EQUAL_INT32(-1512000000, r.lon_e7);  // 151°12' W
    TEST_ASSERT_EQUAL_UINT32(0, r.time_utc_ms);
}

static void test_parse_no_fix_sentence_is_ok_but_invalid(void)
{
    // Cold NEO-6M output: everything blank, status V.
    char       s[128];
    nmea_rmc_t r;
    mk(s, sizeof(s), "GPRMC,,V,,,,,,,,,,N");
    TEST_ASSERT_TRUE(nmea_parse_rmc(s, &r));
    TEST_ASSERT_FALSE(r.valid);
    TEST_ASSERT_EQUAL_INT32(0, r.lat_e7);
    TEST_ASSERT_EQUAL_UINT16(0, r.speed_mph);
}

static void test_parse_blank_speed_and_course_default_to_zero(void)
{
    char       s[128];
    nmea_rmc_t r;
    mk(s, sizeof(s), "GPRMC,120000,A,5926.2200,N,02445.2160,E,,,030726");
    TEST_ASSERT_TRUE(nmea_parse_rmc(s, &r));
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_UINT16(0, r.speed_mph);
    TEST_ASSERT_EQUAL_UINT16(0, r.heading_deg);
}

static void test_parse_coordinate_without_decimal_point(void)
{
    char       s[128];
    nmea_rmc_t r;
    mk(s, sizeof(s), "GPRMC,120000,A,5930,N,02430,E,1.0,90.0,030726");
    TEST_ASSERT_TRUE(nmea_parse_rmc(s, &r));
    TEST_ASSERT_EQUAL_INT32(595000000, r.lat_e7);  // 59°30' = 59.5°
    TEST_ASSERT_EQUAL_INT32(245000000, r.lon_e7);
}

static void test_parse_time_with_fraction_and_course_wrap(void)
{
    char       s[128];
    nmea_rmc_t r;
    // .25s of time fraction; course 360.0 must wrap to heading 0.
    mk(s, sizeof(s), "GPRMC,235959.250,A,0000.0000,N,00000.0000,E,0.5,360.0,030726");
    TEST_ASSERT_TRUE(nmea_parse_rmc(s, &r));
    TEST_ASSERT_EQUAL_UINT32(((23 * 60 + 59) * 60 + 59) * 1000 + 250, r.time_utc_ms);
    TEST_ASSERT_EQUAL_UINT16(0, r.heading_deg);
    TEST_ASSERT_EQUAL_UINT16(1, r.speed_mph);  // 0.5 kn = 0.575 mph → 1
}

static void test_parse_speed_clamps_to_uint16(void)
{
    char       s[128];
    nmea_rmc_t r;
    // 60000 kn ≈ 69046 mph, past the uint16 ceiling (40000 kn would fit in
    // mph, so it needs to be higher than the old km/h-era value).
    mk(s, sizeof(s), "GPRMC,120000,A,0000.0000,N,00000.0000,E,60000,0.0,030726");
    TEST_ASSERT_TRUE(nmea_parse_rmc(s, &r));
    TEST_ASSERT_EQUAL_UINT16(65535, r.speed_mph);
}

static void test_parse_minutes_precision_truncates_past_1e5(void)
{
    char       s[128];
    nmea_rmc_t r;
    // 7 fraction digits of minutes — beyond the carried 1e-5 precision.
    mk(s, sizeof(s), "GPRMC,120000,A,1000.1234567,N,02000.0000,E,0.0,0.0,030726");
    TEST_ASSERT_TRUE(nmea_parse_rmc(s, &r));
    // 10° + 0.1234500'/60 (truncated) ≈ 10.0020575°
    TEST_ASSERT_EQUAL_INT32(100020575, r.lat_e7);
}

// --- parser: the rejection paths --------------------------------------

static void test_parse_rejects_framing_and_checksum_problems(void)
{
    nmea_rmc_t r;
    TEST_ASSERT_FALSE(nmea_parse_rmc("GPRMC,120000,V*33", &r));         // no '$'
    TEST_ASSERT_FALSE(nmea_parse_rmc("$GPRMC,120000,V", &r));           // no '*'
    TEST_ASSERT_FALSE(nmea_parse_rmc("$GPRMC,,V,,,,,,,,,,N*ZZ", &r));   // hex above 'F'
    TEST_ASSERT_FALSE(nmea_parse_rmc("$GPRMC,,V,,,,,,,,,,N*5Z", &r));   // bad hex lo
    TEST_ASSERT_FALSE(nmea_parse_rmc("$GPRMC,,V,,,,,,,,,,N*!!", &r));   // hex below '0'
    TEST_ASSERT_FALSE(nmea_parse_rmc("$GPRMC,,V,,,,,,,,,,N*53X", &r));  // trailing junk
    TEST_ASSERT_FALSE(nmea_parse_rmc("$GPRMC,,V,,,,,,,,,,N*54", &r));   // wrong sum
}

static void test_parse_rejects_wrong_sentence_shapes(void)
{
    char       s[128];
    nmea_rmc_t r;
    mk(s, sizeof(s), "GPGGA,120000,5926.22,N,02445.21,E,1,08,0.9,20.0,M,,,,");
    TEST_ASSERT_FALSE(nmea_parse_rmc(s, &r));  // not RMC
    mk(s, sizeof(s), "RMC,120000,A,0000.0000,N,00000.0000,E,0.0,0.0,030726");
    TEST_ASSERT_FALSE(nmea_parse_rmc(s, &r));  // 3-char type
    mk(s, sizeof(s), "GPRMC,120000,A");
    TEST_ASSERT_FALSE(nmea_parse_rmc(s, &r));  // too few fields
    mk(s, sizeof(s), "GPRMC,120000,,0000.0000,N,00000.0000,E,0.0,0.0,030726");
    TEST_ASSERT_FALSE(nmea_parse_rmc(s, &r));  // empty status
    mk(s, sizeof(s), "GPRMC,120000,X,0000.0000,N,00000.0000,E,0.0,0.0,030726");
    TEST_ASSERT_FALSE(nmea_parse_rmc(s, &r));  // unknown status
}

static void test_parse_rejects_bad_time(void)
{
    char        s[128];
    nmea_rmc_t  r;
    const char *bad[] = {
        "GPRMC,,A,0000.0000,N,00000.0000,E,0.0,0.0,030726",         // empty
        "GPRMC,12345,A,0000.0000,N,00000.0000,E,0.0,0.0,030726",    // 5 digits
        "GPRMC,240000,A,0000.0000,N,00000.0000,E,0.0,0.0,030726",   // hh > 23
        "GPRMC,126000,A,0000.0000,N,00000.0000,E,0.0,0.0,030726",   // mm > 59
        "GPRMC,120060,A,0000.0000,N,00000.0000,E,0.0,0.0,030726",   // ss > 59
        "GPRMC,.123456,A,0000.0000,N,00000.0000,E,0.0,0.0,030726",  // leading dot
        "GPRMC,12.34.5,A,0000.0000,N,00000.0000,E,0.0,0.0,030726",  // double dot
        "GPRMC,12h519,A,0000.0000,N,00000.0000,E,0.0,0.0,030726",   // non-digit
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        mk(s, sizeof(s), bad[i]);
        TEST_ASSERT_FALSE_MESSAGE(nmea_parse_rmc(s, &r), bad[i]);
    }
}

static void test_parse_rejects_bad_coordinates(void)
{
    char        s[128];
    nmea_rmc_t  r;
    const char *bad[] = {
        "GPRMC,120000,A,943.0000,N,00000.0000,E,0.0,0.0,030726",    // lat int part 3 digits
        "GPRMC,120000,A,0060.0000,N,00000.0000,E,0.0,0.0,030726",   // minutes = 60
        "GPRMC,120000,A,00x0.0000,N,00000.0000,E,0.0,0.0,030726",   // non-digit minutes
        "GPRMC,120000,A,9A43.0000,N,00000.0000,E,0.0,0.0,030726",   // letter in degrees
        "GPRMC,120000,A,-943.0000,N,00000.0000,E,0.0,0.0,030726",   // signed degrees
        "GPRMC,120000,A,0000.0000,Q,00000.0000,E,0.0,0.0,030726",   // bad lat hemi
        "GPRMC,120000,A,0000.0000,,00000.0000,E,0.0,0.0,030726",    // empty lat hemi
        "GPRMC,120000,A,0000.0000,N,0000.0000,E,0.0,0.0,030726",    // lon int part 4 digits
        "GPRMC,120000,A,0000.0000,N,00000.0000,QQ,0.0,0.0,030726",  // 2-char lon hemi
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        mk(s, sizeof(s), bad[i]);
        TEST_ASSERT_FALSE_MESSAGE(nmea_parse_rmc(s, &r), bad[i]);
    }
}

static void test_parse_rejects_bad_speed_and_course(void)
{
    char        s[128];
    nmea_rmc_t  r;
    const char *bad[] = {
        "GPRMC,120000,A,0000.0000,N,00000.0000,E,1O.0,0.0,030726",   // letter O in speed
        "GPRMC,120000,A,0000.0000,N,00000.0000,E,-1.0,0.0,030726",   // negative speed
        "GPRMC,120000,A,0000.0000,N,00000.0000,E,0.0,NaN,030726",    // garbage course
        "GPRMC,120000,A,0000.0000,N,00000.0000,E,0.0,361.0,030726",  // course > 360
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        mk(s, sizeof(s), bad[i]);
        TEST_ASSERT_FALSE_MESSAGE(nmea_parse_rmc(s, &r), bad[i]);
    }
}

void RunTests(void)
{
    RUN_TEST(test_framer_assembles_sentence_and_strips_crlf);
    RUN_TEST(test_framer_drops_noise_between_sentences);
    RUN_TEST(test_framer_resyncs_on_dollar_mid_sentence);
    RUN_TEST(test_framer_accepts_bare_lf_terminator);
    RUN_TEST(test_framer_discards_oversized_lines);
    RUN_TEST(test_parse_datasheet_example);
    RUN_TEST(test_parse_southern_western_hemispheres);
    RUN_TEST(test_parse_no_fix_sentence_is_ok_but_invalid);
    RUN_TEST(test_parse_blank_speed_and_course_default_to_zero);
    RUN_TEST(test_parse_coordinate_without_decimal_point);
    RUN_TEST(test_parse_time_with_fraction_and_course_wrap);
    RUN_TEST(test_parse_speed_clamps_to_uint16);
    RUN_TEST(test_parse_minutes_precision_truncates_past_1e5);
    RUN_TEST(test_parse_rejects_framing_and_checksum_problems);
    RUN_TEST(test_parse_rejects_wrong_sentence_shapes);
    RUN_TEST(test_parse_rejects_bad_time);
    RUN_TEST(test_parse_rejects_bad_coordinates);
    RUN_TEST(test_parse_rejects_bad_speed_and_course);
}
