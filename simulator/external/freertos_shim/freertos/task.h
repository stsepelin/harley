#pragma once
#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn,
                                   const char *name,
                                   uint32_t stack_depth,
                                   void *param,
                                   uint32_t priority,
                                   TaskHandle_t *handle,
                                   int core);

void vTaskDelay(TickType_t ticks);
