#include "j1850_edge.h"

void j1850_edge_init(j1850_edge_t *e)
{
    e->level = -1;
}

bool j1850_edge_level(j1850_edge_t *e, uint32_t dur_us, bool *active)
{
    if (dur_us > J1850_VPW_SOF_MAX_US) {
        // Recessive idle: an absolute anchor. The ended pulse was passive
        // (LOW); the next edge rises to dominant.
        *active  = false;
        e->level = 1;
        return true;
    }
    if (e->level < 0)
        return false;  // phase not anchored yet — drop the pulse
    *active  = (e->level != 0);
    e->level = (int8_t)(!e->level);
    return true;
}
