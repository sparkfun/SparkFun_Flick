/*
 * baseline_rtos.h - FreeRTOS's names for sketches (BL_HAS_RTOS: the ESP32
 * family), on Baseline's task_*, sem_* and queue_* entries (ABI 1.10).
 *
 * Included by baseline.h, so Arduino-ESP32 code that uses xTaskCreate,
 * xSemaphoreTake, xQueueSend, ... without including anything compiles as it
 * is; <freertos/FreeRTOS.h>, <freertos/task.h>, <freertos/semphr.h> and
 * <freertos/queue.h> include this header too.
 *
 * Differences from ESP-IDF's FreeRTOS:
 *   - One tick is one millisecond (configTICK_RATE_HZ 1000, as in Arduino-ESP32).
 *   - Handles are plain pointers (void *).
 *   - Sketch code never runs in an interrupt, so the ...FromISR calls are the
 *     ordinary ones and *pxHigherPriorityTaskWoken is set to pdFALSE.
 *   - Priorities are capped at BL_TASK_MAX_PRIORITY (10) and stacks are at
 *     least BL_TASK_MIN_STACK (3072) bytes.
 *   - Callbacks (Ticker, ESP-NOW, WiFi events, ...) run only on the sketch's own
 *     task, inside its delay()/yield() and after loop(); delay() in another
 *     task only waits.
 */
#pragma once
#include "baseline_api.h"

#if defined(BL_HAS_RTOS)

#ifdef __cplusplus
extern "C" {
#endif

typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef uint8_t StackType_t;            /* ESP-IDF counts stacks in bytes */
typedef void (*TaskFunction_t)(void *);

#define pdTRUE                  1
#define pdFALSE                 0
#define pdPASS                  1
#define pdFAIL                  0
#define errQUEUE_FULL           0
#define errQUEUE_EMPTY          0
#define portMAX_DELAY           ((TickType_t)BL_WAIT_FOREVER)
#define configTICK_RATE_HZ      1000
#define portTICK_PERIOD_MS      ((TickType_t)1)
#define portTICK_RATE_MS        portTICK_PERIOD_MS
#define pdMS_TO_TICKS(ms)       ((TickType_t)(ms))
#define pdTICKS_TO_MS(ticks)    ((uint32_t)(ticks))
#define tskIDLE_PRIORITY        0
#define tskNO_AFFINITY          0x7FFFFFFF
#define configMAX_PRIORITIES    25
#define configMINIMAL_STACK_SIZE 768
#define taskYIELD()             yield()
#define portYIELD_FROM_ISR(...) do { } while (0)

/* ---- tasks ---- */

/* In sdk/src/sketch_rtos.c: the first call also tells the callback pump
 * which task is the sketch's own. core: 0, 1 or tskNO_AFFINITY. */
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack_bytes, void *arg,
                                   UBaseType_t priority, TaskHandle_t *created, BaseType_t core);

static inline BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack_bytes, void *arg,
                                     UBaseType_t priority, TaskHandle_t *created)
{
    return xTaskCreatePinnedToCore(fn, name, stack_bytes, arg, priority, created, tskNO_AFFINITY);
}

static inline void vTaskDelete(TaskHandle_t task) { bl_task_delete(task); }

/* ESP-IDF's versions that take the stack from other memory: caps
 * MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT (esp_heap_caps.h) puts it in PSRAM, which
 * leaves internal RAM for WiFi and Bluetooth. A task with a PSRAM stack must
 * never write flash itself (LittleFS, Preferences, Update): ESP-IDF stops with
 * an assert. Without such memory the stack is in internal RAM. In
 * sdk/src/sketch_rtos.c. */
BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t fn, const char *name, uint32_t stack_bytes, void *arg,
                                           UBaseType_t priority, TaskHandle_t *created, BaseType_t core,
                                           UBaseType_t caps);

static inline BaseType_t xTaskCreateWithCaps(TaskFunction_t fn, const char *name, uint32_t stack_bytes, void *arg,
                                             UBaseType_t priority, TaskHandle_t *created, UBaseType_t caps)
{
    return xTaskCreatePinnedToCoreWithCaps(fn, name, stack_bytes, arg, priority, created, tskNO_AFFINITY, caps);
}

static inline void vTaskDeleteWithCaps(TaskHandle_t task) { bl_task_delete(task); }
static inline void vTaskDelay(TickType_t ticks) { delay(ticks); }
static inline TickType_t xTaskGetTickCount(void) { return millis(); }
static inline TickType_t xTaskGetTickCountFromISR(void) { return millis(); }

static inline BaseType_t xTaskDelayUntil(TickType_t *previous, TickType_t increment)
{
    TickType_t next = *previous + increment;
    int32_t wait = (int32_t)(next - millis());
    *previous = next;
    if (wait > 0) {
        delay((uint32_t)wait);
        return pdTRUE;
    }
    return pdFALSE;
}
static inline void vTaskDelayUntil(TickType_t *previous, TickType_t increment) { xTaskDelayUntil(previous, increment); }

static inline TaskHandle_t xTaskGetCurrentTaskHandle(void) { return bl_task_current(); }
static inline char *pcTaskGetName(TaskHandle_t task) { return (char *)bl_task_name(task); }
static inline char *pcTaskGetTaskName(TaskHandle_t task) { return (char *)bl_task_name(task); }
static inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { return bl_task_stack_free(task); }
static inline UBaseType_t uxTaskPriorityGet(TaskHandle_t task) { return bl_task_priority(task, -1); }
static inline void vTaskPrioritySet(TaskHandle_t task, UBaseType_t priority) { bl_task_priority(task, (int)priority); }
static inline void vTaskSuspend(TaskHandle_t task) { bl_task_suspend(task, 1); }
static inline void vTaskResume(TaskHandle_t task) { bl_task_suspend(task, 0); }
static inline UBaseType_t uxTaskGetNumberOfTasks(void) { return bl_task_info(BL_TASK_INFO_COUNT); }
static inline BaseType_t xPortGetCoreID(void) { return bl_task_info(BL_TASK_INFO_CORE); }
static inline size_t xPortGetFreeHeapSize(void) { return bl_free_heap(); }
static inline size_t xPortGetMinimumEverFreeHeapSize(void) { return bl_system_info(BL_INFO_MIN_FREE_HEAP); }

/* ---- semaphores and mutexes ---- */

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return bl_sem_create(BL_SEM_MUTEX, 1, 1); }
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return bl_sem_create(BL_SEM_RECURSIVE, 1, 1); }
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) { return bl_sem_create(BL_SEM_BINARY, 1, 0); }
static inline SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t initial)
{
    return bl_sem_create(BL_SEM_COUNTING, max, initial);
}
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks) { return bl_sem_take(sem, ticks, 0); }
static inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t ticks) { return bl_sem_take(sem, ticks, 1); }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) { return bl_sem_give(sem, 0); }
static inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t sem) { return bl_sem_give(sem, 1); }
static inline UBaseType_t uxSemaphoreGetCount(SemaphoreHandle_t sem) { return bl_sem_count(sem); }
static inline void vSemaphoreDelete(SemaphoreHandle_t sem) { bl_sem_delete(sem); }

static inline BaseType_t xSemaphoreTakeFromISR(SemaphoreHandle_t sem, BaseType_t *woken)
{
    if (woken) {
        *woken = pdFALSE;
    }
    return bl_sem_take(sem, 0, 0);
}
static inline BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t sem, BaseType_t *woken)
{
    if (woken) {
        *woken = pdFALSE;
    }
    return bl_sem_give(sem, 0);
}

/* ---- queues ---- */

static inline QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) { return bl_queue_create(length, item_size); }
static inline BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks) { return bl_queue_send(q, item, ticks, BL_QUEUE_BACK); }
static inline BaseType_t xQueueSendToBack(QueueHandle_t q, const void *item, TickType_t ticks) { return bl_queue_send(q, item, ticks, BL_QUEUE_BACK); }
static inline BaseType_t xQueueSendToFront(QueueHandle_t q, const void *item, TickType_t ticks) { return bl_queue_send(q, item, ticks, BL_QUEUE_FRONT); }
static inline BaseType_t xQueueOverwrite(QueueHandle_t q, const void *item) { return bl_queue_send(q, item, 0, BL_QUEUE_OVERWRITE); }
static inline BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks) { return bl_queue_receive(q, item, ticks, 0); }
static inline BaseType_t xQueuePeek(QueueHandle_t q, void *item, TickType_t ticks) { return bl_queue_receive(q, item, ticks, 1); }
static inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return bl_queue_count(q, 0); }
static inline UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q) { return bl_queue_count(q, 1); }
static inline BaseType_t xQueueReset(QueueHandle_t q) { return bl_queue_reset(q); }
static inline void vQueueDelete(QueueHandle_t q) { bl_queue_delete(q); }

static inline BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *woken)
{
    if (woken) {
        *woken = pdFALSE;
    }
    return bl_queue_send(q, item, 0, BL_QUEUE_BACK);
}
static inline BaseType_t xQueueSendToBackFromISR(QueueHandle_t q, const void *item, BaseType_t *woken)
{
    return xQueueSendFromISR(q, item, woken);
}
static inline BaseType_t xQueueSendToFrontFromISR(QueueHandle_t q, const void *item, BaseType_t *woken)
{
    if (woken) {
        *woken = pdFALSE;
    }
    return bl_queue_send(q, item, 0, BL_QUEUE_FRONT);
}
static inline BaseType_t xQueueOverwriteFromISR(QueueHandle_t q, const void *item, BaseType_t *woken)
{
    if (woken) {
        *woken = pdFALSE;
    }
    return bl_queue_send(q, item, 0, BL_QUEUE_OVERWRITE);
}
static inline BaseType_t xQueueReceiveFromISR(QueueHandle_t q, void *item, BaseType_t *woken)
{
    if (woken) {
        *woken = pdFALSE;
    }
    return bl_queue_receive(q, item, 0, 0);
}

#ifdef __cplusplus
}
#endif

#endif /* BL_HAS_RTOS */
