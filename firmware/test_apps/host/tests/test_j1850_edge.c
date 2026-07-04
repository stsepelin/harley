// Toggling edge->level tracker (the no-pin-read RX candidate). The
// acceptance gate from docs/j1850-toggling-isr-candidate.md: on a clean
// stream the reconstructed levels match the physical bus AND decode to a
// valid frame; startup drops until the first idle anchor; and an injected
// missed OR spurious edge corrupts at most one frame — the next
// inter-frame idle re-syncs, so the following frame decodes clean.

#include "unity.h"
#include "j1850_edge.h"
#include "j1850_vpw.h"
#include <string.h>

#define IDLE_US 300  // > SOF_MAX: an idle anchor

// Physical pulses for one frame: (optional leading idle) SOF+bits, then a
// trailing EOD+idle. payload is WITHOUT the CRC; the CRC is appended.
static size_t build(const uint8_t *payload, size_t plen, j1850_pulse_t *out, size_t cap, bool lead)
{
    uint8_t frame[J1850_MAX_FRAME];
    memcpy(frame, payload, plen);
    frame[plen] = j1850_crc(payload, plen);

    size_t n = 0;
    if (lead)
        out[n++] = (j1850_pulse_t){.active = false, .dur_us = IDLE_US};
    size_t e = j1850_vpw_encode(frame, plen + 1, out + n, cap - n);
    TEST_ASSERT_TRUE(e > 0);
    n += e;
    out[n++] = (j1850_pulse_t){.active = false, .dur_us = IDLE_US};
    return n;
}

// Run physical pulses through the tracker, feed the reconstructed
// (level, dur) to the decoder. Returns frames decoded (last in *f);
// with check_levels, asserts each reconstructed level matches physical.
static size_t run(const j1850_pulse_t *phys, size_t n, j1850_frame_t *f, bool check_levels)
{
    j1850_edge_t e;
    j1850_edge_init(&e);
    j1850_vpw_rx_t rx;
    j1850_vpw_rx_init(&rx);
    size_t frames = 0;
    for (size_t i = 0; i < n; i++) {
        bool active;
        if (!j1850_edge_level(&e, phys[i].dur_us, &active))
            continue;  // dropped before the first anchor
        if (check_levels)
            TEST_ASSERT_EQUAL_MESSAGE(phys[i].active, active, "reconstructed level mismatch");
        if (j1850_vpw_rx_pulse(&rx, active, phys[i].dur_us, f))
            frames++;
    }
    return frames;
}

static void test_clean_stream_reconstructs_levels_and_decodes(void)
{
    const uint8_t msg[] = {0x28, 0x1B, 0x10, 0x02, 0x13, 0xC4};
    j1850_pulse_t p[128];
    size_t        n = build(msg, sizeof(msg), p, 128, true);
    j1850_frame_t f;
    TEST_ASSERT_EQUAL_size_t(1, run(p, n, &f, true));
    TEST_ASSERT_TRUE(f.crc_ok);
    TEST_ASSERT_EQUAL_size_t(7, f.len);
}

static void test_startup_drops_until_first_idle_anchor(void)
{
    j1850_edge_t e;
    j1850_edge_init(&e);
    bool a;
    TEST_ASSERT_FALSE(j1850_edge_level(&e, 64, &a));   // no anchor yet -> dropped
    TEST_ASSERT_FALSE(j1850_edge_level(&e, 128, &a));  // still no anchor
    TEST_ASSERT_TRUE(j1850_edge_level(&e, IDLE_US, &a));
    TEST_ASSERT_FALSE(a);                             // idle held recessive LOW
    TEST_ASSERT_TRUE(j1850_edge_level(&e, 200, &a));  // SOF right after
    TEST_ASSERT_TRUE(a);                              // dominant HIGH
}

static void test_idle_threshold_boundary(void)
{
    j1850_edge_t e;
    bool         a;
    // Exactly SOF_MAX is a valid symbol width, not an anchor -> pre-anchor drop.
    j1850_edge_init(&e);
    TEST_ASSERT_FALSE(j1850_edge_level(&e, J1850_VPW_SOF_MAX_US, &a));
    // One tick over is the idle anchor.
    j1850_edge_init(&e);
    TEST_ASSERT_TRUE(j1850_edge_level(&e, J1850_VPW_SOF_MAX_US + 1, &a));
    TEST_ASSERT_FALSE(a);
}

// Frame A gets an edge removed (two mid-frame pulses merged); frame B
// follows after an idle. B must decode clean regardless of A.
static void test_missed_edge_resyncs_next_frame(void)
{
    const uint8_t A[] = {0x48, 0xDA, 0x40, 0x39, 0x02};
    const uint8_t B[] = {0x28, 0x1B, 0x10, 0x02, 0x13, 0xC4};
    j1850_pulse_t p[256];

    size_t na = build(A, sizeof(A), p, 256, true);
    size_t k  = 4;  // a bit pulse inside frame A
    p[k].dur_us += p[k + 1].dur_us;
    memmove(&p[k + 1], &p[k + 2], (na - (k + 2)) * sizeof(p[0]));
    na--;
    size_t n = na + build(B, sizeof(B), p + na, 256 - na, true);

    j1850_frame_t f;
    TEST_ASSERT_TRUE(run(p, n, &f, false) >= 1);
    TEST_ASSERT_TRUE(f.crc_ok);
    uint8_t expect[J1850_MAX_FRAME];
    memcpy(expect, B, sizeof(B));
    expect[sizeof(B)] = j1850_crc(B, sizeof(B));
    TEST_ASSERT_EQUAL_size_t(sizeof(B) + 1, f.len);
    TEST_ASSERT_EQUAL_MEMORY(expect, f.data, f.len);
}

// Frame A gets a spurious edge inserted (a pulse split in two); B follows.
static void test_extra_edge_resyncs_next_frame(void)
{
    const uint8_t A[] = {0x48, 0xDA, 0x40, 0x39, 0x02};
    const uint8_t B[] = {0x28, 0x1B, 0x10, 0x02, 0x13, 0xC4};
    j1850_pulse_t p[256];

    size_t na = build(A, sizeof(A), p, 256, true);
    size_t k  = 4;
    memmove(&p[k + 1], &p[k], (na - k) * sizeof(p[0]));
    uint16_t d      = p[k].dur_us;
    p[k].dur_us     = (uint16_t)(d / 2);
    p[k + 1].dur_us = (uint16_t)(d - d / 2);
    na++;
    size_t n = na + build(B, sizeof(B), p + na, 256 - na, true);

    j1850_frame_t f;
    TEST_ASSERT_TRUE(run(p, n, &f, false) >= 1);
    TEST_ASSERT_TRUE(f.crc_ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(B) + 1, f.len);
    TEST_ASSERT_EQUAL_HEX8(0x28, f.data[0]);
}

void RunTests(void)
{
    RUN_TEST(test_clean_stream_reconstructs_levels_and_decodes);
    RUN_TEST(test_startup_drops_until_first_idle_anchor);
    RUN_TEST(test_idle_threshold_boundary);
    RUN_TEST(test_missed_edge_resyncs_next_frame);
    RUN_TEST(test_extra_edge_resyncs_next_frame);
}
