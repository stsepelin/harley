#include "smooth.h"

int32_t smooth_step(int32_t current, int32_t target)
{
    int32_t diff = target - current;
    int32_t step = diff / 4;
    if (diff != 0 && step == 0) {
        step = (diff > 0) ? 1 : -1;
    }
    return current + step;
}
