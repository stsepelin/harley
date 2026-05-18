// pthread-backed implementations of the FreeRTOS surface the firmware uses.
// Real mutexes + real threads so the sim task and the LVGL/UI thread can
// run concurrently (which is the whole point of having a desktop sim).

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <pthread.h>
#include <stdlib.h>
#include <time.h>

// --- Mutexes ---------------------------------------------------------------
// xSemaphoreCreateMutex returns an opaque handle; we allocate a real
// pthread_mutex_t behind it.

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    pthread_mutex_t *m = malloc(sizeof(*m));
    if (!m) return NULL;
    pthread_mutex_init(m, NULL);
    return m;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait_ticks)
{
    (void)wait_ticks;
    // FreeRTOS semantics allow a timeout; for the sim we just block.
    // Contention is brief (single memcpy of vehicle_data_t).
    if (pthread_mutex_lock((pthread_mutex_t *)s) == 0) return pdTRUE;
    return pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    return pthread_mutex_unlock((pthread_mutex_t *)s) == 0 ? pdTRUE : pdFALSE;
}

// --- Tasks -----------------------------------------------------------------
// xTaskCreatePinnedToCore spawns a detached pthread. We don't honour the
// pinning or priority — on a 10-core MacBook it doesn't matter.

typedef struct {
    TaskFunction_t fn;
    void *param;
} task_args_t;

static void *task_trampoline(void *raw)
{
    task_args_t *a = (task_args_t *)raw;
    TaskFunction_t fn = a->fn;
    void *param = a->param;
    free(a);
    fn(param);
    return NULL;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn,
                                   const char *name,
                                   uint32_t stack_depth,
                                   void *param,
                                   uint32_t priority,
                                   TaskHandle_t *handle,
                                   int core)
{
    (void)name; (void)stack_depth; (void)priority; (void)core;

    task_args_t *a = malloc(sizeof(*a));
    if (!a) return pdFAIL;
    a->fn = fn;
    a->param = param;

    pthread_t tid;
    if (pthread_create(&tid, NULL, task_trampoline, a) != 0) {
        free(a);
        return pdFAIL;
    }
    pthread_detach(tid);
    if (handle) *handle = (TaskHandle_t)(uintptr_t)tid;
    return pdPASS;
}

// --- Time ------------------------------------------------------------------

void vTaskDelay(TickType_t ticks)
{
    // pdMS_TO_TICKS(ms) is just `ms` in our shim, so ticks here == milliseconds.
    struct timespec ts = {
        .tv_sec  = ticks / 1000,
        .tv_nsec = (long)(ticks % 1000) * 1000000L,
    };
    nanosleep(&ts, NULL);
}
