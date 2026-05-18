#pragma once
// Desktop FreeRTOS shim. Real pthread-backed mutexes and threads — the sim
// task and the LVGL/UI thread run truly concurrently, so we can't use the
// always-succeeds stub from the host test app here.
#include <stdint.h>
#include <stddef.h>

typedef int      BaseType_t;
typedef uint32_t TickType_t;

#define pdTRUE         ((BaseType_t)1)
#define pdFALSE        ((BaseType_t)0)
#define pdPASS         pdTRUE
#define pdFAIL         pdFALSE
#define portMAX_DELAY  ((TickType_t)0xffffffffUL)

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
