#include "telemetry_codec.h"

static size_t put_u8(uint8_t *b, size_t o, uint8_t v)
{
    b[o] = v;
    return o + 1;
}

static size_t put_u16(uint8_t *b, size_t o, uint16_t v)
{
    b[o]     = (uint8_t)(v & 0xFFu);
    b[o + 1] = (uint8_t)((v >> 8) & 0xFFu);
    return o + 2;
}

static size_t put_u32(uint8_t *b, size_t o, uint32_t v)
{
    b[o]     = (uint8_t)(v & 0xFFu);
    b[o + 1] = (uint8_t)((v >> 8) & 0xFFu);
    b[o + 2] = (uint8_t)((v >> 16) & 0xFFu);
    b[o + 3] = (uint8_t)((v >> 24) & 0xFFu);
    return o + 4;
}

// Bools pack branchlessly (each is already 0/1) so the only branch in the
// encoder is the size guard - keeps the pure test at 100% without a case per
// lamp bit.
static uint16_t pack_lamps(const vehicle_data_t *vd)
{
    return (uint16_t)(((uint16_t)vd->turn_left << 0) | ((uint16_t)vd->turn_right << 1) |
                      ((uint16_t)vd->low_beam << 2) | ((uint16_t)vd->high_beam << 3) |
                      ((uint16_t)vd->neutral << 4) | ((uint16_t)vd->oil_pressure_warning << 5) |
                      ((uint16_t)vd->check_engine << 6) | ((uint16_t)vd->abs_warning << 7) |
                      ((uint16_t)vd->battery_warning << 8) |
                      ((uint16_t)vd->immobiliser_warning << 9));
}

size_t telemetry_encode(const vehicle_data_t *vd, uint8_t status, uint8_t *out, size_t out_sz)
{
    if (out_sz < TELEMETRY_FRAME_LEN)
        return 0;

    size_t o = 0;
    o        = put_u8(out, o, (uint8_t)TELEMETRY_TYPE);
    o        = put_u16(out, o, (uint16_t)TELEMETRY_PAYLOAD_LEN);
    o        = put_u16(out, o, vd->speed_raw);
    o        = put_u16(out, o, vd->speed_mph);
    o        = put_u16(out, o, vd->rpm);
    o        = put_u8(out, o, (uint8_t)vd->gear);
    o        = put_u8(out, o, (uint8_t)vd->engine_temp_c);
    o        = put_u8(out, o, vd->fuel_level);
    o        = put_u16(out, o, pack_lamps(vd));
    o        = put_u32(out, o, vd->odometer_m);
    o        = put_u32(out, o, vd->trip1_m);
    o        = put_u32(out, o, vd->trip2_m);
    o        = put_u32(out, o, vd->trip1_fuel_ticks);
    o        = put_u32(out, o, vd->trip2_fuel_ticks);
    o        = put_u8(out, o, vd->clock_hours);
    o        = put_u8(out, o, vd->clock_minutes);
    o        = put_u8(out, o, status);
    return o;
}
