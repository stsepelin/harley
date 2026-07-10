#include "gps_source.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static gps_source_t      s_data;
static SemaphoreHandle_t s_mutex;

void gps_source_init(void)
{
    memset(&s_data, 0, sizeof(s_data));
    s_mutex = xSemaphoreCreateMutex();
}

void gps_source_set(const gps_source_t *new_data)
{
    if (!new_data)
        return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_data = *new_data;
        xSemaphoreGive(s_mutex);
    }
}

void gps_source_get(gps_source_t *out)
{
    if (!out)
        return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        *out = s_data;
        xSemaphoreGive(s_mutex);
    }
}
