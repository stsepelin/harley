#include "sim_math.h"

float integrate_distance_m(float speed_kmh, float tick_s)
{
    return (speed_kmh / 3.6f) * tick_s;
}

float clock_advance(float seconds, float delta_s, float wrap_s)
{
    seconds += delta_s;
    if (seconds >= wrap_s) seconds -= wrap_s;
    return seconds;
}

void clock_seconds_to_hm(float seconds, uint8_t *out_h, uint8_t *out_m)
{
    if (seconds < 0.0f) seconds = 0.0f;
    int total_min = (int)(seconds / 60.0f);
    if (total_min >= 24 * 60) total_min %= 24 * 60;
    *out_h = (uint8_t)(total_min / 60);
    *out_m = (uint8_t)(total_min % 60);
}

bool fuel_tick(float *progress, uint8_t *level,
               float tick_s, float step_s, uint8_t max_level)
{
    *progress += tick_s;
    if (*progress < step_s) return false;
    *progress -= step_s;
    if (*level == 0) *level = max_level;
    else             (*level)--;
    return true;
}
