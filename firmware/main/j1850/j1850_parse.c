#include "j1850_parse.h"
#include <string.h>

// A message matches when the frame is at least min_len bytes and its
// leading id_len bytes equal id. min_len >= id_len always, so the length
// check also guards the memcmp.
static bool msg(const j1850_frame_t *f, const uint8_t *id, size_t id_len, size_t min_len)
{
    return f->len >= min_len && memcmp(f->data, id, id_len) == 0;
}

static gear_t decode_gear(uint8_t x)
{
    switch (x) {
    case 0x00:
        return GEAR_NEUTRAL;
    case 0x01:
        return GEAR_1;
    case 0x03:
        return GEAR_2;
    case 0x07:
        return GEAR_3;
    case 0x0F:
        return GEAR_4;
    case 0x1F:
        return GEAR_5;
    case 0x3F:
        return GEAR_6;
    default:
        return GEAR_UNKNOWN;
    }
}

bool j1850_parse(const j1850_frame_t *f, vehicle_data_t *vd)
{
    if (!f->crc_ok)
        return false;

    static const uint8_t RPM[]   = {0x28, 0x1B, 0x10, 0x02};
    static const uint8_t TEMP[]  = {0xA8, 0x49, 0x10, 0x10};
    static const uint8_t GEAR[]  = {0xA8, 0x3B, 0x10, 0x03};
    static const uint8_t SPEED[] = {0x48, 0x29, 0x10, 0x02};
    static const uint8_t TURN[]  = {0x48, 0xDA, 0x40, 0x39};
    static const uint8_t CEL[]   = {0x68, 0x88, 0x10};

    if (msg(f, RPM, 4, 7)) {
        vd->rpm = (uint16_t)(((f->data[4] << 8) | f->data[5]) / 4);
        return true;
    }
    if (msg(f, TEMP, 4, 6)) {
        vd->engine_temp_c = (int8_t)f->data[4];  // raw byte = degrees C
        return true;
    }
    if (msg(f, GEAR, 4, 6)) {
        vd->gear    = decode_gear(f->data[4]);
        vd->neutral = (vd->gear == GEAR_NEUTRAL);
        return true;
    }
    if (msg(f, SPEED, 4, 7)) {
        vd->speed_kmh = (uint16_t)(((f->data[4] << 8) | f->data[5]) / J1850_SPEED_DIVISOR);
        return true;
    }
    if (msg(f, TURN, 4, 6)) {
        // This bike: bit1 = left, bit0 = right (swapped vs HarleyDroid).
        vd->turn_left  = (f->data[4] & 0x02) != 0;
        vd->turn_right = (f->data[4] & 0x01) != 0;
        return true;
    }
    if (msg(f, CEL, 3, 5)) {
        vd->check_engine = (f->data[3] & 0x80) != 0;  // 68 88 10 83 = on
        return true;
    }
    return false;
}
