// J1850 TX driver pure logic: frame build (CRC append), the watchdog
// dominant-length guard, and on-air duration. The build helper is
// round-tripped through the real encoder + decoder so the CRC it appends
// is provably the one the receiver accepts — the host-side mirror of the
// on-device self-sniff loop.

#include "unity.h"
#include "j1850_tx_logic.h"
#include "j1850_vpw.h"
#include <string.h>

// --- build_frame --------------------------------------------------------

static void test_build_frame_appends_the_crc_the_decoder_accepts(void)
{
    const uint8_t payload[] = {0x68, 0xFF, 0x40, 0x03, 0xD8};  // an IM keep-alive
    uint8_t       frame[J1850_MAX_FRAME];
    size_t        len = 0;
    TEST_ASSERT_TRUE(j1850_tx_build_frame(payload, sizeof(payload), frame, sizeof(frame), &len));
    TEST_ASSERT_EQUAL_size_t(sizeof(payload) + 1, len);
    TEST_ASSERT_EQUAL_MEMORY(payload, frame, sizeof(payload));
    TEST_ASSERT_EQUAL_HEX8(j1850_crc(payload, sizeof(payload)), frame[len - 1]);

    // Encode -> decode: the built frame must come back CRC-valid.
    j1850_pulse_t pulses[128];
    size_t        n = j1850_vpw_encode(frame, len, pulses, 128);
    TEST_ASSERT_TRUE(n > 0);
    pulses[n++] = (j1850_pulse_t){.active = false, .dur_us = 300};  // EOF

    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);
    j1850_frame_t f;
    size_t        got = 0;
    for (size_t i = 0; i < n; i++)
        if (j1850_vpw_rx_pulse(&rx, pulses[i].active, pulses[i].dur_us, &f))
            got++;
    TEST_ASSERT_EQUAL_size_t(1, got);
    TEST_ASSERT_EQUAL_size_t(len, f.len);
    TEST_ASSERT_EQUAL_MEMORY(frame, f.data, len);
    TEST_ASSERT_TRUE(f.crc_ok);
}

static void test_build_frame_rejects_bad_sizes(void)
{
    uint8_t out[J1850_MAX_FRAME];
    size_t  len = 123;

    // Empty payload.
    uint8_t one = 0x11;
    TEST_ASSERT_FALSE(j1850_tx_build_frame(&one, 0, out, sizeof(out), &len));

    // Payload + CRC would exceed the 12-byte SAE frame limit.
    uint8_t twelve[12];
    memset(twelve, 0xC3, sizeof(twelve));
    TEST_ASSERT_FALSE(j1850_tx_build_frame(twelve, sizeof(twelve), out, sizeof(out), &len));

    // Output buffer too small for payload + CRC.
    uint8_t two[2] = {0x28, 0x1B};
    TEST_ASSERT_FALSE(j1850_tx_build_frame(two, sizeof(two), out, 2, &len));

    TEST_ASSERT_EQUAL_size_t(123, len);  // untouched on every failure
}

// --- watchdog dominant-length guard -------------------------------------

static void test_stream_within_limits_accepts_a_real_frame(void)
{
    uint8_t       msg[3] = {0x29, 0xFE, 0x40};
    j1850_pulse_t pulses[64];
    size_t        n = j1850_vpw_encode(msg, sizeof(msg), pulses, 64);
    TEST_ASSERT_TRUE(n > 0);  // SOF 200us + bit pulses, all <= dominant max
    TEST_ASSERT_TRUE(j1850_tx_stream_within_limits(pulses, n));
}

static void test_stream_within_limits_rejects_over_long_dominant(void)
{
    // A passive pulse over the limit is fine (recessive can't jam); an
    // active one over the limit is the fault the watchdog exists for.
    j1850_pulse_t ok[] = {
        {.active = true, .dur_us = 200},                             // SOF, at the ceiling side
        {.active = false, .dur_us = J1850_TX_DOMINANT_MAX_US + 50},  // long recessive: allowed
        {.active = true, .dur_us = J1850_TX_DOMINANT_MAX_US},        // exactly the limit: allowed
    };
    TEST_ASSERT_TRUE(j1850_tx_stream_within_limits(ok, sizeof(ok) / sizeof(ok[0])));

    j1850_pulse_t bad[] = {
        {.active = true, .dur_us = 200},
        {.active = true, .dur_us = J1850_TX_DOMINANT_MAX_US + 1},  // one tick over: reject
    };
    TEST_ASSERT_FALSE(j1850_tx_stream_within_limits(bad, sizeof(bad) / sizeof(bad[0])));
}

// --- on-air duration ----------------------------------------------------

static void test_stream_duration_sums_all_pulses(void)
{
    j1850_pulse_t p[] = {
        {.active = true, .dur_us = 200},
        {.active = false, .dur_us = 64},
        {.active = true, .dur_us = 128},
    };
    TEST_ASSERT_EQUAL_UINT32(392, j1850_tx_stream_duration_us(p, sizeof(p) / sizeof(p[0])));
    TEST_ASSERT_EQUAL_UINT32(0, j1850_tx_stream_duration_us(p, 0));
}

void RunTests(void)
{
    RUN_TEST(test_build_frame_appends_the_crc_the_decoder_accepts);
    RUN_TEST(test_build_frame_rejects_bad_sizes);
    RUN_TEST(test_stream_within_limits_accepts_a_real_frame);
    RUN_TEST(test_stream_within_limits_rejects_over_long_dominant);
    RUN_TEST(test_stream_duration_sums_all_pulses);
}
