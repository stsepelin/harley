#include "gear_table.h"

gear_t gear_for_speed(float speed_kmh, float *out_rpm)
{
    if (speed_kmh < 15) {
        *out_rpm = 900 + (speed_kmh / 15.0f) * 5000;
        return GEAR_1;
    }
    if (speed_kmh < 35) {
        *out_rpm = 2500 + ((speed_kmh - 15.0f) / 20.0f) * 4000;
        return GEAR_2;
    }
    if (speed_kmh < 60) {
        *out_rpm = 2800 + ((speed_kmh - 35.0f) / 25.0f) * 4000;
        return GEAR_3;
    }
    if (speed_kmh < 90) {
        *out_rpm = 3000 + ((speed_kmh - 60.0f) / 30.0f) * 5000;
        return GEAR_4;
    }
    if (speed_kmh < 115) {
        *out_rpm = 3500 + ((speed_kmh - 90.0f) / 25.0f) * 5000;
        return GEAR_5;
    }
    *out_rpm = 4500 + ((speed_kmh - 115.0f) / 15.0f) * 5500;
    return GEAR_6;
}
