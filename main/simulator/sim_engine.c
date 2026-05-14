#include "sim_engine.h"
#include "vehicle_data.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void sim_task(void *arg)
{
    float t = 0.0f;
    vehicle_data_t data = {
        .engine_temp_c = 92,
        .fuel_level = 4,
        .odometer_m = 12847000,
    };

    while (1) {
        t += 0.05f;

        float speed = 80.0f + 50.0f * sinf(t * 0.3f);     // 30..130 km/h
        float rpm = 2000.0f + speed * 30.0f;              // proportional to speed

        data.speed_kmh = (uint16_t)speed;
        data.rpm = (uint16_t)rpm;

        if      (speed < 20)  data.gear = GEAR_1;
        else if (speed < 40)  data.gear = GEAR_2;
        else if (speed < 60)  data.gear = GEAR_3;
        else if (speed < 90)  data.gear = GEAR_4;
        else if (speed < 120) data.gear = GEAR_5;
        else                  data.gear = GEAR_6;

        data.turn_left = ((int)(t * 2) % 6) < 2;

        vehicle_data_set(&data);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void sim_engine_start(void)
{
    xTaskCreatePinnedToCore(sim_task, "sim", 4096, NULL, 5, NULL, 0);
}
