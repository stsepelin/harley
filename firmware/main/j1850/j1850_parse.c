#include "j1850_parse.h"
#include <string.h>

// A message matches when the frame is at least min_len bytes and its
// leading id_len bytes equal id. min_len >= id_len always, so the length
// check also guards the memcmp.
static bool msg(const j1850_frame_t *f, const uint8_t *id, size_t id_len, size_t min_len)
{
    return f->len >= min_len && memcmp(f->data, id, id_len) == 0;
}

bool j1850_parse(const j1850_frame_t *f, vehicle_data_t *vd)
{
    if (!f->crc_ok)
        return false;

    static const uint8_t RPM[]   = {0x28, 0x1B, 0x10, 0x02};
    static const uint8_t TEMP[]  = {0xA8, 0x49, 0x10, 0x10};
    static const uint8_t SPEED[] = {0x48, 0x29, 0x10, 0x02};
    static const uint8_t TURN[]  = {0x48, 0xDA, 0x40, 0x39};
    static const uint8_t CEL[]   = {0x68, 0x88, 0x10};

    if (msg(f, RPM, 4, 7)) {
        vd->rpm = (uint16_t)(((f->data[4] << 8) | f->data[5]) / 4);
        return true;
    }
    if (msg(f, TEMP, 4, 6)) {
        // Confirmed on ride 1 (firmware/docs/ride-1-findings.md): the OBD-style
        // offset. Cold-start raw 0x3F (63) at ~20-25 C ambient climbed to ~0x81
        // (129) fully warm; raw-40 gives 23 C cold / 89 C hot, both correct.
        // engine_temp_c is int8_t; the realistic bus range (0x00..~0xA7) maps
        // to -40..+127 C, so no clamp is needed for any temperature the engine
        // will show.
        vd->engine_temp_c = (int8_t)((int)f->data[4] - 40);
        return true;
    }
    // NB: A8 3B 10 was previously decoded here as a gear ladder — that was
    // wrong (this bike has no gear sensor; ride 1 showed A8 3B 10 is an
    // engine-load/throttle value). Gear is now computed from the RPM:speed
    // ratio in gear_calc (called by j1850_driver); neutral is a separate switch
    // bit (48 3B 40), decoded once confirmed on the bike.
    if (msg(f, SPEED, 4, 7)) {
        uint16_t raw  = (uint16_t)((f->data[4] << 8) | f->data[5]);
        vd->speed_raw = raw;
        vd->speed_mph = (uint16_t)(raw / J1850_SPEED_DIVISOR);
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
