#include "j1850_tx_logic.h"

bool j1850_tx_build_frame(const uint8_t *payload, size_t n, uint8_t *out, size_t out_cap,
                          size_t *out_len)
{
    if (n == 0 || n + 1 > J1850_MAX_FRAME || n + 1 > out_cap)
        return false;
    for (size_t i = 0; i < n; i++)
        out[i] = payload[i];
    out[n]   = j1850_crc(payload, n);
    *out_len = n + 1;
    return true;
}

bool j1850_tx_stream_within_limits(const j1850_pulse_t *pulses, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (pulses[i].active && pulses[i].dur_us > J1850_TX_DOMINANT_MAX_US)
            return false;
    return true;
}

uint32_t j1850_tx_stream_duration_us(const j1850_pulse_t *pulses, size_t n)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < n; i++)
        sum += pulses[i].dur_us;
    return sum;
}
