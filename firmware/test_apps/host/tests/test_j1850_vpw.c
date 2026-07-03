// J1850 VPW codec. The encoder is the fixture generator for the
// decoder: every happy-path test round-trips encode → decode, so the
// two sides can't drift apart. The CRC is anchored to the published
// CRC-8/SAE-J1850 check value; everything else is self-consistent.

#include "unity.h"
#include "j1850_vpw.h"
#include <string.h>

#define EOD_US 200  // passive, EOD width (163 < t <= 239)
#define EOF_US 300  // passive, EOF width (> 239)

// Feed pulses; returns how many frames were emitted, last one in *out.
static size_t feed(j1850_vpw_rx_t *rx, const j1850_pulse_t *p, size_t n, j1850_frame_t *out)
{
    size_t frames = 0;
    for (size_t i = 0; i < n; i++) {
        if (j1850_vpw_rx_pulse(rx, p[i].active, p[i].dur_us, out))
            frames++;
    }
    return frames;
}

// Encode data + append a terminating passive pulse of the given width.
static size_t encode_with_tail(const uint8_t *data, size_t len, j1850_pulse_t *out, size_t cap,
                               uint16_t tail_us)
{
    size_t n = j1850_vpw_encode(data, len, out, cap);
    TEST_ASSERT_TRUE(n > 0);
    out[n++] = (j1850_pulse_t){.active = false, .dur_us = tail_us};
    return n;
}

// --- CRC ---------------------------------------------------------------

static void test_crc_matches_published_check_value(void)
{
    // CRC-8/SAE-J1850 catalogue entry: check("123456789") = 0x4B.
    const uint8_t nine[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX8(0x4B, j1850_crc(nine, sizeof(nine)));
}

// --- encode ↔ decode round trips ----------------------------------------

static void test_round_trip_rpm_style_frame(void)
{
    // Shaped like the decode table's RPM broadcast: header + 2 data
    // bytes + CRC. 0xC3-ish byte values exercise all four VPW bit
    // encodings (short/long × active/passive).
    uint8_t msg[6] = {0x28, 0x1B, 0x10, 0x02, 0xC3, 0};
    msg[5]         = j1850_crc(msg, 5);

    j1850_pulse_t pulses[64];
    size_t        n = encode_with_tail(msg, sizeof(msg), pulses, 64, EOF_US);

    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);
    j1850_frame_t f;
    TEST_ASSERT_EQUAL_size_t(1, feed(&rx, pulses, n, &f));
    TEST_ASSERT_EQUAL_size_t(6, f.len);
    TEST_ASSERT_EQUAL_MEMORY(msg, f.data, 6);
    TEST_ASSERT_TRUE(f.crc_ok);
    TEST_ASSERT_EQUAL_size_t(0, f.ifr_len);
}

static void test_bad_crc_is_flagged_not_dropped(void)
{
    // The sniffer wants to SEE corrupted frames (they're a wiring
    // health signal), so the decoder emits with crc_ok = false.
    uint8_t msg[4] = {0x48, 0x29, 0x10, 0x77};  // wrong CRC on purpose
    TEST_ASSERT_NOT_EQUAL(0x77, j1850_crc(msg, 3));

    j1850_pulse_t pulses[64];
    size_t        n = encode_with_tail(msg, sizeof(msg), pulses, 64, EOF_US);

    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);
    j1850_frame_t f;
    TEST_ASSERT_EQUAL_size_t(1, feed(&rx, pulses, n, &f));
    TEST_ASSERT_FALSE(f.crc_ok);
}

static void test_eod_then_ifr_bytes_attach_to_frame(void)
{
    uint8_t msg[3] = {0xA8, 0x3B, 0};
    msg[2]         = j1850_crc(msg, 2);

    j1850_pulse_t pulses[64];
    size_t        n = j1850_vpw_encode(msg, sizeof(msg), pulses, 64);
    TEST_ASSERT_TRUE(n > 0);
    pulses[n++] = (j1850_pulse_t){false, EOD_US};  // EOD, not EOF
    pulses[n++] = (j1850_pulse_t){true, 64};       // IFR normalization bit
    // One IFR byte 0x55: bits 01010101 starting on a passive pulse.
    for (int b = 7; b >= 0; b--) {
        bool     active = (7 - b) % 2 != 0;
        uint8_t  bit    = (0x55 >> b) & 1;
        uint16_t dur    = active ? (bit ? 64 : 128) : (bit ? 128 : 64);
        pulses[n++]     = (j1850_pulse_t){active, dur};
    }
    pulses[n++] = (j1850_pulse_t){false, EOF_US};

    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);
    j1850_frame_t f;
    TEST_ASSERT_EQUAL_size_t(1, feed(&rx, pulses, n, &f));
    TEST_ASSERT_TRUE(f.crc_ok);
    TEST_ASSERT_EQUAL_size_t(1, f.ifr_len);
    TEST_ASSERT_EQUAL_HEX8(0x55, f.ifr[0]);
}

static void test_eod_then_eof_emits_without_ifr(void)
{
    uint8_t msg[3] = {0x68, 0x88, 0};
    msg[2]         = j1850_crc(msg, 2);

    j1850_pulse_t pulses[64];
    size_t        n = j1850_vpw_encode(msg, sizeof(msg), pulses, 64);
    TEST_ASSERT_TRUE(n > 0);
    pulses[n++] = (j1850_pulse_t){false, EOD_US};
    pulses[n++] = (j1850_pulse_t){false, EOF_US};  // idle-timeout synth

    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);
    j1850_frame_t f;
    TEST_ASSERT_EQUAL_size_t(1, feed(&rx, pulses, n, &f));
    TEST_ASSERT_EQUAL_size_t(0, f.ifr_len);
    TEST_ASSERT_TRUE(f.crc_ok);
}

static void test_back_to_back_frames_sof_right_after_eod(void)
{
    uint8_t a[3] = {0x10, 0x20, 0};
    a[2]         = j1850_crc(a, 2);
    uint8_t b[3] = {0x30, 0x40, 0};
    b[2]         = j1850_crc(b, 2);

    j1850_pulse_t pulses[128];
    size_t        n = j1850_vpw_encode(a, sizeof(a), pulses, 128);
    TEST_ASSERT_TRUE(n > 0);
    pulses[n++] = (j1850_pulse_t){false, EOD_US};
    // Second frame's SOF lands straight out of POST_EOD.
    size_t m = j1850_vpw_encode(b, sizeof(b), pulses + n, 128 - n);
    TEST_ASSERT_TRUE(m > 0);
    n += m;
    pulses[n++] = (j1850_pulse_t){false, EOF_US};

    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);
    j1850_frame_t f;
    TEST_ASSERT_EQUAL_size_t(2, feed(&rx, pulses, n, &f));
    TEST_ASSERT_EQUAL_MEMORY(b, f.data, 3);  // *out holds the last frame
    TEST_ASSERT_TRUE(f.crc_ok);
}

static void test_post_eod_garbage_still_emits_the_finished_frame(void)
{
    uint8_t msg[3] = {0x11, 0x22, 0};
    msg[2]         = j1850_crc(msg, 2);

    j1850_pulse_t pulses[64];
    size_t        n = j1850_vpw_encode(msg, sizeof(msg), pulses, 64);
    TEST_ASSERT_TRUE(n > 0);
    pulses[n++] = (j1850_pulse_t){false, EOD_US};
    pulses[n++] = (j1850_pulse_t){true, 500};  // stuck-active garbage

    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);
    j1850_frame_t f;
    TEST_ASSERT_EQUAL_size_t(1, feed(&rx, pulses, n, &f));
    TEST_ASSERT_TRUE(f.crc_ok);
}

// --- resync + rejection paths -------------------------------------------

static void test_idle_ignores_noise_until_sof(void)
{
    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);
    j1850_frame_t f;
    // Bit-width actives, huge actives, and passives must all be inert.
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, true, 64, &f));
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, true, 128, &f));
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, true, 400, &f));
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, false, 200, &f));
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, false, 10000, &f));
}

static void test_sof_mid_frame_resyncs_to_the_new_frame(void)
{
    uint8_t msg[3] = {0x55, 0xAA, 0};
    msg[2]         = j1850_crc(msg, 2);

    j1850_pulse_t pulses[64];
    size_t        n = 0;
    pulses[n++]     = (j1850_pulse_t){true, 200};  // SOF of a frame that dies
    pulses[n++]     = (j1850_pulse_t){false, 64};  // one stray bit pulse...
    pulses[n++]     = (j1850_pulse_t){true, 64};
    // ...then a fresh SOF: the earlier fragment must be discarded.
    size_t m = j1850_vpw_encode(msg, sizeof(msg), pulses + n, 64 - n);
    TEST_ASSERT_TRUE(m > 0);
    n += m;
    pulses[n++] = (j1850_pulse_t){false, EOF_US};

    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);
    j1850_frame_t f;
    TEST_ASSERT_EQUAL_size_t(1, feed(&rx, pulses, n, &f));
    TEST_ASSERT_EQUAL_MEMORY(msg, f.data, 3);
    TEST_ASSERT_TRUE(f.crc_ok);
}

static void test_stuck_active_mid_frame_drops_to_idle(void)
{
    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);
    j1850_frame_t f;
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, true, 200, &f));  // SOF
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, false, 64, &f));
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, true, 400, &f));  // > SOF max
    // Decoder must be back in idle: a long passive emits nothing.
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, false, EOF_US, &f));
}

static void test_partial_byte_at_eod_is_dropped(void)
{
    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);
    j1850_frame_t f;
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, true, 200, &f));  // SOF
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, false, 64, &f));  // 4 bits only
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, true, 64, &f));
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, false, 128, &f));
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, true, 128, &f));
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, false, EOF_US, &f));
}

static void test_sof_with_no_data_is_dropped(void)
{
    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);
    j1850_frame_t f;
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, true, 200, &f));
    TEST_ASSERT_FALSE(j1850_vpw_rx_pulse(&rx, false, EOF_US, &f));
}

static void test_over_length_frame_is_dropped(void)
{
    // 13 bytes breaks the 12-byte SAE limit — decoder must bail, and
    // the trailing EOF must not emit anything.
    uint8_t big[13];
    memset(big, 0xC3, sizeof(big));

    j1850_pulse_t pulses[128];
    size_t        n = j1850_vpw_encode(big, sizeof(big), pulses, 128);
    TEST_ASSERT_EQUAL_size_t(0, n);  // encoder refuses it too

    // Hand-build: 13 bytes of bits after SOF.
    n           = 0;
    pulses[n++] = (j1850_pulse_t){true, 200};
    bool active = false;
    for (int i = 0; i < 13 * 8; i++) {
        pulses[n++] = (j1850_pulse_t){active, 64};
        active      = !active;
    }
    pulses[n++] = (j1850_pulse_t){false, EOF_US};

    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);
    j1850_frame_t f;
    TEST_ASSERT_EQUAL_size_t(0, feed(&rx, pulses, n, &f));
}

static void test_partial_ifr_byte_is_dropped_whole(void)
{
    uint8_t msg[3] = {0x11, 0x22, 0};
    msg[2]         = j1850_crc(msg, 2);

    j1850_pulse_t pulses[64];
    size_t        n = j1850_vpw_encode(msg, sizeof(msg), pulses, 64);
    TEST_ASSERT_TRUE(n > 0);
    pulses[n++] = (j1850_pulse_t){false, EOD_US};
    pulses[n++] = (j1850_pulse_t){true, 64};   // normalization bit
    pulses[n++] = (j1850_pulse_t){false, 64};  // 2 IFR bits — not a byte
    pulses[n++] = (j1850_pulse_t){true, 64};
    pulses[n++] = (j1850_pulse_t){false, EOF_US};

    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);
    j1850_frame_t f;
    // A mangled IFR poisons the exchange — nothing is emitted.
    TEST_ASSERT_EQUAL_size_t(0, feed(&rx, pulses, n, &f));
}

// --- encoder argument checks ---------------------------------------------

static void test_encode_rejects_bad_arguments(void)
{
    uint8_t       one = 0xFF;
    j1850_pulse_t out[16];
    TEST_ASSERT_EQUAL_size_t(0, j1850_vpw_encode(&one, 0, out, 16));  // empty
    TEST_ASSERT_EQUAL_size_t(0, j1850_vpw_encode(&one, 1, out, 8));   // cap < 9
    uint8_t       big[13] = {0};
    j1850_pulse_t bigout[128];
    TEST_ASSERT_EQUAL_size_t(0, j1850_vpw_encode(big, 13, bigout, 128));  // > 12
}

void RunTests(void)
{
    RUN_TEST(test_crc_matches_published_check_value);
    RUN_TEST(test_round_trip_rpm_style_frame);
    RUN_TEST(test_bad_crc_is_flagged_not_dropped);
    RUN_TEST(test_eod_then_ifr_bytes_attach_to_frame);
    RUN_TEST(test_eod_then_eof_emits_without_ifr);
    RUN_TEST(test_back_to_back_frames_sof_right_after_eod);
    RUN_TEST(test_post_eod_garbage_still_emits_the_finished_frame);
    RUN_TEST(test_idle_ignores_noise_until_sof);
    RUN_TEST(test_sof_mid_frame_resyncs_to_the_new_frame);
    RUN_TEST(test_stuck_active_mid_frame_drops_to_idle);
    RUN_TEST(test_partial_byte_at_eod_is_dropped);
    RUN_TEST(test_sof_with_no_data_is_dropped);
    RUN_TEST(test_over_length_frame_is_dropped);
    RUN_TEST(test_partial_ifr_byte_is_dropped_whole);
    RUN_TEST(test_encode_rejects_bad_arguments);
}
