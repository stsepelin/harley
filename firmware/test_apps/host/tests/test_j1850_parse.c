// J1850 message decoder. Fixtures are REAL frames pulled from the
// 2026-07-04 on-bike capture (firmware/docs/captures/) — each is checked
// to be genuinely CRC-valid before it's fed to the parser, so the decode
// table is anchored to the bus, not to hand-invented bytes. Synthetic
// frames (built with a computed CRC) cover the gear ladder and the speed
// math, which the parked capture couldn't exercise.

#include "unity.h"
#include "j1850_parse.h"
#include "j1850_vpw.h"
#include "vehicle_data.h"
#include <string.h>

// A captured frame INCLUDING its CRC byte; crc_ok is derived, then
// asserted true so a bad fixture fails loudly.
static j1850_frame_t real(const uint8_t *bytes, size_t len)
{
    j1850_frame_t f;
    memset(&f, 0, sizeof(f));
    memcpy(f.data, bytes, len);
    f.len    = len;
    f.crc_ok = (j1850_crc(bytes, len - 1) == bytes[len - 1]);
    TEST_ASSERT_TRUE_MESSAGE(f.crc_ok, "fixture is not a valid captured frame");
    return f;
}

// A synthetic frame from a payload WITHOUT the CRC; CRC is appended.
static j1850_frame_t synth(const uint8_t *payload, size_t n)
{
    j1850_frame_t f;
    memset(&f, 0, sizeof(f));
    memcpy(f.data, payload, n);
    f.data[n] = j1850_crc(payload, n);
    f.len     = n + 1;
    f.crc_ok  = true;
    return f;
}

// --- real-frame decodes -------------------------------------------------

static void test_rpm_from_real_frames(void)
{
    vehicle_data_t vd = {0};
    // 28 1B 10 02 HH LL CRC -> (HH<<8|LL)/4
    const uint8_t idle[]   = {0x28, 0x1B, 0x10, 0x02, 0x13, 0xC4, 0xF4};
    const uint8_t engoff[] = {0x28, 0x1B, 0x10, 0x02, 0x00, 0x00, 0xD5};
    j1850_frame_t f        = real(idle, sizeof(idle));
    TEST_ASSERT_TRUE(j1850_parse(&f, &vd));
    TEST_ASSERT_EQUAL_UINT16(1265, vd.rpm);  // 0x13C4 / 4
    f = real(engoff, sizeof(engoff));
    TEST_ASSERT_TRUE(j1850_parse(&f, &vd));
    TEST_ASSERT_EQUAL_UINT16(0, vd.rpm);
}

static void test_engine_temp_from_real_frame(void)
{
    vehicle_data_t vd  = {0};
    const uint8_t  t[] = {0xA8, 0x49, 0x10, 0x10, 0x40, 0xAF};  // raw 0x40=64 -> 64-40 = 24 C
    j1850_frame_t  f   = real(t, sizeof(t));
    TEST_ASSERT_TRUE(j1850_parse(&f, &vd));
    TEST_ASSERT_EQUAL_INT8(24, vd.engine_temp_c);
}

static void test_a83b10_is_not_gear(void)
{
    // A8 3B 10 is engine-load/throttle, not a gear ladder (this bike has no
    // gear sensor). The parser must not recognise it or touch gear/neutral;
    // gear is computed from the RPM:speed ratio in gear_calc (the driver).
    vehicle_data_t vd  = {.gear = GEAR_3, .neutral = false};
    const uint8_t  g[] = {0xA8, 0x3B, 0x10, 0x03, 0x00, 0x00};
    j1850_frame_t  f   = real(g, sizeof(g));
    TEST_ASSERT_FALSE(j1850_parse(&f, &vd));
    TEST_ASSERT_EQUAL(GEAR_3, vd.gear);  // untouched
    TEST_ASSERT_FALSE(vd.neutral);
}

static void test_turn_signals_from_real_frames(void)
{
    // 48 DA 40 39 XX CRC — bit1 = left, bit0 = right on this bike.
    const uint8_t  off[]   = {0x48, 0xDA, 0x40, 0x39, 0x00, 0x70};
    const uint8_t  right[] = {0x48, 0xDA, 0x40, 0x39, 0x01, 0x6D};
    const uint8_t  left[]  = {0x48, 0xDA, 0x40, 0x39, 0x02, 0x4A};
    const uint8_t  both[]  = {0x48, 0xDA, 0x40, 0x39, 0x03, 0x57};
    vehicle_data_t vd      = {0};
    j1850_frame_t  f;

    f = real(left, sizeof(left));
    TEST_ASSERT_TRUE(j1850_parse(&f, &vd));
    TEST_ASSERT_TRUE(vd.turn_left);
    TEST_ASSERT_FALSE(vd.turn_right);

    f = real(right, sizeof(right));
    TEST_ASSERT_TRUE(j1850_parse(&f, &vd));
    TEST_ASSERT_FALSE(vd.turn_left);
    TEST_ASSERT_TRUE(vd.turn_right);

    f = real(both, sizeof(both));
    TEST_ASSERT_TRUE(j1850_parse(&f, &vd));
    TEST_ASSERT_TRUE(vd.turn_left);
    TEST_ASSERT_TRUE(vd.turn_right);

    f = real(off, sizeof(off));
    TEST_ASSERT_TRUE(j1850_parse(&f, &vd));
    TEST_ASSERT_FALSE(vd.turn_left);
    TEST_ASSERT_FALSE(vd.turn_right);
}

static void test_check_engine_from_real_frames(void)
{
    const uint8_t  off[] = {0x68, 0x88, 0x10, 0x03, 0x44};
    const uint8_t  on[]  = {0x68, 0x88, 0x10, 0x83, 0x62};  // bit7 set = on
    vehicle_data_t vd    = {0};
    j1850_frame_t  f;

    f = real(on, sizeof(on));
    TEST_ASSERT_TRUE(j1850_parse(&f, &vd));
    TEST_ASSERT_TRUE(vd.check_engine);

    f = real(off, sizeof(off));
    TEST_ASSERT_TRUE(j1850_parse(&f, &vd));
    TEST_ASSERT_FALSE(vd.check_engine);
}

static void test_speed_parked_is_zero(void)
{
    vehicle_data_t vd  = {0};
    const uint8_t  s[] = {0x48, 0x29, 0x10, 0x02, 0x00, 0x00, 0x56};
    j1850_frame_t  f   = real(s, sizeof(s));
    TEST_ASSERT_TRUE(j1850_parse(&f, &vd));
    TEST_ASSERT_EQUAL_UINT16(0, vd.speed_mph);
}

// --- synthetic frames (speed math) --------------------------------------

static void test_speed_math_nonzero(void)
{
    vehicle_data_t vd   = {0};
    uint8_t        pl[] = {0x48, 0x29, 0x10, 0x02, 0x40, 0x00};  // 0x4000=16384 / 195 = 84 mph
    j1850_frame_t  f    = synth(pl, sizeof(pl));
    TEST_ASSERT_TRUE(j1850_parse(&f, &vd));
    TEST_ASSERT_EQUAL_UINT16(84, vd.speed_mph);
}

// --- rejection paths ----------------------------------------------------

static void test_unrecognised_frame_is_ignored(void)
{
    // 29 FE 40 01 64 — an IM keep-alive, not a vehicle-data broadcast.
    const uint8_t  ka[] = {0x29, 0xFE, 0x40, 0x01, 0x64};
    vehicle_data_t vd   = {.rpm = 4321, .gear = GEAR_3};
    j1850_frame_t  f    = real(ka, sizeof(ka));
    TEST_ASSERT_FALSE(j1850_parse(&f, &vd));
    TEST_ASSERT_EQUAL_UINT16(4321, vd.rpm);  // untouched
    TEST_ASSERT_EQUAL(GEAR_3, vd.gear);
}

static void test_bad_crc_frame_is_ignored(void)
{
    uint8_t       bytes[] = {0x28, 0x1B, 0x10, 0x02, 0x13, 0xC4, 0x00};  // CRC wrong
    j1850_frame_t f;
    memset(&f, 0, sizeof(f));
    memcpy(f.data, bytes, sizeof(bytes));
    f.len             = sizeof(bytes);
    f.crc_ok          = false;
    vehicle_data_t vd = {.rpm = 999};
    TEST_ASSERT_FALSE(j1850_parse(&f, &vd));
    TEST_ASSERT_EQUAL_UINT16(999, vd.rpm);
}

static void test_prefix_match_but_too_short_is_ignored(void)
{
    // 28 1B 10 02 with no data bytes — the RPM prefix, but shorter than
    // a real RPM frame; must not read past the end.
    uint8_t        pl[] = {0x28, 0x1B, 0x10, 0x02};
    j1850_frame_t  f    = synth(pl, sizeof(pl));  // len 5 (payload + CRC)
    vehicle_data_t vd   = {.rpm = 111};
    TEST_ASSERT_FALSE(j1850_parse(&f, &vd));
    TEST_ASSERT_EQUAL_UINT16(111, vd.rpm);
}

void RunTests(void)
{
    RUN_TEST(test_rpm_from_real_frames);
    RUN_TEST(test_engine_temp_from_real_frame);
    RUN_TEST(test_a83b10_is_not_gear);
    RUN_TEST(test_turn_signals_from_real_frames);
    RUN_TEST(test_check_engine_from_real_frames);
    RUN_TEST(test_speed_parked_is_zero);
    RUN_TEST(test_speed_math_nonzero);
    RUN_TEST(test_unrecognised_frame_is_ignored);
    RUN_TEST(test_bad_crc_frame_is_ignored);
    RUN_TEST(test_prefix_match_but_too_short_is_ignored);
}
