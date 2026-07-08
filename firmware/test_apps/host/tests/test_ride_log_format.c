#include "unity.h"
#include "ride_log_format.h"
#include <string.h>

// Build a frame directly; ride_log_format trusts the crc_ok flag as given
// (it formats, it does not re-verify the CRC), so fixtures set it explicitly.
static j1850_frame_t mk(const uint8_t *bytes, size_t len, bool crc_ok)
{
    j1850_frame_t f = {0};
    memcpy(f.data, bytes, len);
    f.len    = len;
    f.crc_ok = crc_ok;
    return f;
}

// --- decoded suffixes ----------------------------------------------------

static void test_speed_line_raw_counts(void)
{
    const uint8_t b[] = {0x48, 0x29, 0x10, 0x02, 0x40, 0x00, 0x56};  // raw 0x4000 = 16384 counts
    j1850_frame_t f   = mk(b, sizeof(b), true);
    char          out[160];
    ride_log_format_line(&f, 1500, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("1.500 j1850: 48 29 10 02 40 00 56 | CRC OK | speed_raw=16384", out);
}

static void test_temp_line_raw_byte(void)
{
    const uint8_t b[] = {0xA8, 0x49, 0x10, 0x10, 0x40, 0x11};
    j1850_frame_t f   = mk(b, sizeof(b), true);
    char          out[160];
    ride_log_format_line(&f, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("0.000 j1850: A8 49 10 10 40 11 | CRC OK | temp=0x40", out);
}

static void test_load_line_raw_byte(void)
{
    const uint8_t b[] = {0xA8, 0x3B, 0x10, 0x03, 0x04, 0x74};  // A8 3B 10 = load, not gear
    j1850_frame_t f   = mk(b, sizeof(b), true);
    char          out[160];
    ride_log_format_line(&f, 12034, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("12.034 j1850: A8 3B 10 03 04 74 | CRC OK | load=0x04", out);
}

// A recognized-but-not-capture header (RPM) gets no decoded suffix.
static void test_other_frame_has_no_suffix(void)
{
    const uint8_t b[] = {0x28, 0x1B, 0x10, 0x02, 0x14, 0x50, 0x77};
    j1850_frame_t f   = mk(b, sizeof(b), true);
    char          out[160];
    ride_log_format_line(&f, 2000, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("2.000 j1850: 28 1B 10 02 14 50 77 | CRC OK", out);
    TEST_ASSERT_NULL(strstr(out, " | speed="));
}

// A too-short speed/temp/gear header must not trip its decode branch.
static void test_short_header_no_suffix(void)
{
    const uint8_t b[] = {0x48, 0x29, 0x10, 0x02, 0x40, 0x00};  // len 6 < 7
    j1850_frame_t f   = mk(b, sizeof(b), true);
    char          out[160];
    ride_log_format_line(&f, 0, out, sizeof(out));
    TEST_ASSERT_NULL(strstr(out, "speed="));
    TEST_ASSERT_EQUAL_STRING("0.000 j1850: 48 29 10 02 40 00 | CRC OK", out);
}

// Bad CRC: raw bytes recorded, verdict BAD, and no decode is trusted.
static void test_bad_crc_no_decode(void)
{
    const uint8_t b[] = {0x48, 0x29, 0x10, 0x02, 0x40, 0x00, 0x99};
    j1850_frame_t f   = mk(b, sizeof(b), false);
    char          out[160];
    ride_log_format_line(&f, 500, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("0.500 j1850: 48 29 10 02 40 00 99 | CRC BAD", out);
}

static void test_ifr_bytes_appended(void)
{
    const uint8_t b[] = {0x68, 0xFF, 0x40, 0x03, 0xD8};
    j1850_frame_t f   = mk(b, sizeof(b), true);
    f.ifr[0]          = 0x11;
    f.ifr[1]          = 0x22;
    f.ifr_len         = 2;
    char out[160];
    ride_log_format_line(&f, 1000, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("1.000 j1850: 68 FF 40 03 D8 | CRC OK | IFR 11 22", out);
}

// --- gear label table (every arm incl. the unknown fallback) -------------

// --- header + edge cases -------------------------------------------------

static void test_header_line(void)
{
    char out[80];
    ride_log_format_header(7, 65432, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("# session boot=7 t=65.432", out);
}

static void test_truncation_reports_needed_length(void)
{
    const uint8_t b[] = {0xA8, 0x3B, 0x10, 0x03, 0x00, 0x33};
    j1850_frame_t f   = mk(b, sizeof(b), true);
    char          small[10];
    int           need = ride_log_format_line(&f, 0, small, sizeof(small));
    TEST_ASSERT_GREATER_OR_EQUAL_INT((int)sizeof(small), need);  // signalled truncation
    TEST_ASSERT_EQUAL_UINT('\0', small[sizeof(small) - 1]);      // still terminated
    TEST_ASSERT_EQUAL_STRING("0.000 j18", small);  // 9 chars + NUL fills the 10-byte buffer
}

static void test_zero_buffer_returns_zero(void)
{
    const uint8_t b[] = {0xA8, 0x3B, 0x10, 0x03, 0x00, 0x33};
    j1850_frame_t f   = mk(b, sizeof(b), true);
    char          dummy;
    TEST_ASSERT_EQUAL_INT(0, ride_log_format_line(&f, 0, &dummy, 0));
    TEST_ASSERT_EQUAL_INT(0, ride_log_format_header(1, 0, &dummy, 0));
}

void RunTests(void)
{
    RUN_TEST(test_speed_line_raw_counts);
    RUN_TEST(test_temp_line_raw_byte);
    RUN_TEST(test_load_line_raw_byte);
    RUN_TEST(test_other_frame_has_no_suffix);
    RUN_TEST(test_short_header_no_suffix);
    RUN_TEST(test_bad_crc_no_decode);
    RUN_TEST(test_ifr_bytes_appended);
    RUN_TEST(test_header_line);
    RUN_TEST(test_truncation_reports_needed_length);
    RUN_TEST(test_zero_buffer_returns_zero);
}
