#include "vehicle_data.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static vehicle_data_t s_data;
static SemaphoreHandle_t s_mutex;

void vehicle_data_init(void)
{
    memset(&s_data, 0, sizeof(s_data));
    s_data.gear = GEAR_NEUTRAL;
    s_mutex = xSemaphoreCreateMutex();
}

void vehicle_data_set(const vehicle_data_t *new_data)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(&s_data, new_data, sizeof(s_data));
        xSemaphoreGive(s_mutex);
    }
}

void vehicle_data_get(vehicle_data_t *out_data)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(out_data, &s_data, sizeof(s_data));
        xSemaphoreGive(s_mutex);
    }
}
