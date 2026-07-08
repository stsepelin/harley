#include "trip_meter.h"

uint16_t trip_meter_delta(trip_meter_t *m, uint16_t raw, uint16_t max_jump)
{
    if (!m->seeded) {
        m->seeded = true;
        m->last   = raw;
        return 0;
    }
    uint16_t d = (uint16_t)(raw - m->last);  // unsigned: 16-bit wrap is correct
    m->last    = raw;
    if (d > max_jump)
        return 0;  // reset / gap — a legitimate wrap is a small delta, not this
    return d;
}
