#include "j1850_vpw.h"
#include <string.h>

enum { RX_IDLE, RX_DATA, RX_POST_EOD, RX_IFR };

uint8_t j1850_crc(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x1D) : (uint8_t)(crc << 1);
        }
    }
    return crc ^ 0xFF;
}

void j1850_vpw_rx_init(j1850_vpw_rx_t *rx)
{
    memset(rx, 0, sizeof(*rx));
}

static void begin_frame(j1850_vpw_rx_t *rx)
{
    memset(&rx->frame, 0, sizeof(rx->frame));
    rx->bits    = 0;
    rx->shifter = 0;
    rx->state   = RX_DATA;
}

// MSB-first into whichever section is accumulating. False = 12-byte
// frame limit blown (protocol violation).
static bool push_bit(j1850_vpw_rx_t *rx, uint8_t bit, uint8_t *buf, size_t *len)
{
    rx->shifter = (uint8_t)((rx->shifter << 1) | bit);
    if (++rx->bits < 8)
        return true;
    if (*len >= J1850_MAX_FRAME)
        return false;
    buf[(*len)++] = rx->shifter;
    rx->bits      = 0;
    rx->shifter   = 0;
    return true;
}

// -1 = not a bit-width pulse. VPW polarity: short active / long passive
// are 1; long active / short passive are 0.
static int classify_bit(bool active, uint32_t dur_us)
{
    if (dur_us <= J1850_VPW_SHORT_MAX_US)
        return active ? 1 : 0;
    if (dur_us <= J1850_VPW_LONG_MAX_US)
        return active ? 0 : 1;
    return -1;
}

static bool emit(j1850_vpw_rx_t *rx, j1850_frame_t *out)
{
    *out      = rx->frame;
    rx->state = RX_IDLE;
    return true;
}

bool j1850_vpw_rx_pulse(j1850_vpw_rx_t *rx, bool active, uint32_t dur_us, j1850_frame_t *out)
{
    if (rx->state == RX_IDLE) {
        // Only an SOF-width active pulse wakes the decoder; everything
        // else is inter-frame idle or noise we resync over.
        if (active && dur_us > J1850_VPW_LONG_MAX_US && dur_us <= J1850_VPW_SOF_MAX_US) {
            begin_frame(rx);
        }
        return false;
    }

    if (rx->state == RX_POST_EOD) {
        // Data section already closed + CRC'd. What follows decides
        // whether there's an IFR, a new frame, or just the bus idling.
        if (active && dur_us <= J1850_VPW_LONG_MAX_US) {
            // IFR normalization bit — discarded, IFR bytes follow.
            rx->bits    = 0;
            rx->shifter = 0;
            rx->state   = RX_IFR;
            return false;
        }
        if (active && dur_us <= J1850_VPW_SOF_MAX_US) {
            // Next frame's SOF arrived right at minimum IFS.
            bool r = emit(rx, out);
            begin_frame(rx);
            return r;
        }
        // Anything else (EOF-length passive, or an out-of-spec active
        // pulse) closes the exchange; the finished frame still stands.
        return emit(rx, out);
    }

    // RX_DATA / RX_IFR — bit sections share the decode rules.
    bool     in_ifr = rx->state == RX_IFR;
    uint8_t *buf    = in_ifr ? rx->frame.ifr : rx->frame.data;
    size_t  *len    = in_ifr ? &rx->frame.ifr_len : &rx->frame.len;

    int bit = classify_bit(active, dur_us);
    if (bit >= 0) {
        if (!push_bit(rx, (uint8_t)bit, buf, len))
            rx->state = RX_IDLE;
        return false;
    }

    if (active) {
        // Over-long active pulse mid-section. SOF-width means we lost
        // the tail of this frame and a new one is starting — resync on
        // it. Longer than SOF is bus garbage.
        if (dur_us <= J1850_VPW_SOF_MAX_US)
            begin_frame(rx);
        else
            rx->state = RX_IDLE;
        return false;
    }

    // Long passive: the section just ended (EOD or EOF). A partial byte
    // means we mis-sampled somewhere — drop the lot.
    if (rx->bits != 0) {
        rx->state = RX_IDLE;
        return false;
    }

    if (in_ifr)
        return emit(rx, out);

    if (rx->frame.len == 0) {  // SOF then silence — nothing real
        rx->state = RX_IDLE;
        return false;
    }
    rx->frame.crc_ok =
        rx->frame.data[rx->frame.len - 1] == j1850_crc(rx->frame.data, rx->frame.len - 1);
    if (dur_us <= J1850_VPW_SOF_MAX_US) {  // EOD width: an IFR may follow
        rx->state = RX_POST_EOD;
        return false;
    }
    return emit(rx, out);  // EOF width: done now
}

size_t j1850_vpw_encode(const uint8_t *data, size_t len, j1850_pulse_t *out, size_t out_cap)
{
    size_t need = 1 + len * 8;
    if (len == 0 || len > J1850_MAX_FRAME || out_cap < need)
        return 0;

    size_t n = 0;
    out[n++] = (j1850_pulse_t){.active = true, .dur_us = 200};  // SOF

    bool active = false;  // level alternates after SOF
    for (size_t i = 0; i < len; i++) {
        for (int b = 7; b >= 0; b--) {
            uint8_t  bit = (data[i] >> b) & 1;
            uint16_t dur;
            if (active)
                dur = bit ? 64 : 128;
            else
                dur = bit ? 128 : 64;
            out[n++] = (j1850_pulse_t){.active = active, .dur_us = dur};
            active   = !active;
        }
    }
    return n;
}
