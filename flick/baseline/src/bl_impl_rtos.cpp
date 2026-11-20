/*
 * bl_impl_rtos.cpp - FreeRTOS tasks, semaphores and queues for sketches
 * (ABI 1.10; the SDK's names are in sdk/include/baseline_rtos.h).
 *
 * Handles are FreeRTOS's own. A sketch's tasks run its code from the sketch
 * window like the sketch task does: never in an interrupt, and stalled with
 * every other task while flash is written. They are not on the task watchdog
 * (FreeRTOS tasks aren't, in Arduino-ESP32 either) unless the sketch adds them
 * with esp_task_wdt_add (bl_impl_idf.cpp); a task that spins on core 0 still
 * trips the idle task's watchdog and counts as a crash.
 *
 * Long waits on the sketch's own task (a mutex, a queue) take it off the
 * watchdog for their length, as other blocking Baseline calls do.
 */
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "gen/bl_impl.h"
#include "bl_console.h"
#include "sketch_loader.h"

/* Waits up to this long don't need the watchdog paused (it fires at 5 s). */
#define SHORT_WAIT_MS   1000

static TickType_t to_ticks(uint32_t ticks)
{
    return ticks == BL_WAIT_FOREVER ? portMAX_DELAY : pdMS_TO_TICKS(ticks);
}

static bool long_wait(uint32_t ticks)
{
    return ticks == BL_WAIT_FOREVER || ticks > SHORT_WAIT_MS;
}

static UBaseType_t capped(int priority)
{
    if (priority < 0) {
        return 0;
    }
    return priority > BL_TASK_MAX_PRIORITY ? BL_TASK_MAX_PRIORITY : priority;
}

/* ---- tasks ---- */

struct TaskStart {
    bl_callback_t fn;
    void *arg;
};

/* A task the sketch put on the task watchdog leaves it before it is deleted,
 * or the watchdog would take the missing feeds for a hang. */
static void twdt_forget(TaskHandle_t t)
{
    if (esp_task_wdt_status(t) == ESP_OK) {
        esp_task_wdt_delete(t);
    }
}

static void task_main(void *p)
{
    TaskStart s = *(TaskStart *)p;
    free(p);
    s.fn(s.arg);
    twdt_forget(NULL);
    vTaskDelete(NULL);      /* a FreeRTOS task must not return */
}

void *bl_impl_task_create(bl_callback_t fn, const char *name, uint32_t stack_bytes, void *arg, int priority, int core)
{
    if (!fn) {
        return NULL;
    }
    TaskStart *s = (TaskStart *)malloc(sizeof(TaskStart));
    if (!s) {
        return NULL;
    }
    s->fn = fn;
    s->arg = arg;
    BaseType_t on = core >= 0 && core < portNUM_PROCESSORS ? core : tskNO_AFFINITY;
    TaskHandle_t t = NULL;
    uint32_t stack = stack_bytes < BL_TASK_MIN_STACK ? BL_TASK_MIN_STACK : stack_bytes;
    BaseType_t r = xTaskCreatePinnedToCore(task_main, name ? name : "sketch_task", stack, s, capped(priority), &t, on);
    if (r != pdPASS) {
        free(s);
        /* Task stacks are in internal RAM: say how much there was */
        BLOG_E("task '%s' (stack %lu, core %d, priority %d) not created: error %d; internal RAM %u free, %u largest block",
               name ? name : "sketch_task", (unsigned long)stack, core, priority, (int)r,
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return NULL;
    }
    return t;
}

void bl_impl_task_delete(void *task)
{
    sketch_task_deleting(task);
    twdt_forget((TaskHandle_t)task);
    vTaskDelete((TaskHandle_t)task);
}

void *bl_impl_task_current(void)
{
    return xTaskGetCurrentTaskHandle();
}

const char *bl_impl_task_name(void *task)
{
    return pcTaskGetName((TaskHandle_t)task);
}

uint32_t bl_impl_task_stack_free(void *task)
{
    return uxTaskGetStackHighWaterMark((TaskHandle_t)task);     /* bytes in ESP-IDF */
}

int bl_impl_task_priority(void *task, int priority)
{
    if (priority >= 0) {
        vTaskPrioritySet((TaskHandle_t)task, capped(priority));
    }
    return uxTaskPriorityGet((TaskHandle_t)task);
}

void bl_impl_task_suspend(void *task, int suspend)
{
    if (suspend) {
        vTaskSuspend((TaskHandle_t)task);
    } else if (task) {
        vTaskResume((TaskHandle_t)task);
    }
}

int bl_impl_task_info(int what)
{
    switch (what) {
    case BL_TASK_INFO_CORE:  return xPortGetCoreID();
    case BL_TASK_INFO_COUNT: return uxTaskGetNumberOfTasks();
    case BL_TASK_INFO_CORES: return portNUM_PROCESSORS;
    default:                 return BL_ERR_ARG;
    }
}

/* ---- semaphores ---- */

void *bl_impl_sem_create(int type, uint32_t max, uint32_t initial)
{
    switch (type) {
    case BL_SEM_MUTEX:     return xSemaphoreCreateMutex();
    case BL_SEM_RECURSIVE: return xSemaphoreCreateRecursiveMutex();
    case BL_SEM_BINARY:    return xSemaphoreCreateBinary();
    case BL_SEM_COUNTING:  return max && initial <= max ? xSemaphoreCreateCounting(max, initial) : NULL;
    default:               return NULL;
    }
}

static int sem_take_now(SemaphoreHandle_t s, TickType_t ticks, int recursive)
{
    return (recursive ? xSemaphoreTakeRecursive(s, ticks) : xSemaphoreTake(s, ticks)) == pdTRUE;
}

int bl_impl_sem_take(void *sem, uint32_t ticks, int recursive)
{
    SemaphoreHandle_t s = (SemaphoreHandle_t)sem;
    if (!s) {
        return 0;
    }
    if (!long_wait(ticks)) {
        return sem_take_now(s, to_ticks(ticks), recursive);
    }
    if (sem_take_now(s, 0, recursive)) {
        return 1;
    }
    WdtPause p;
    return sem_take_now(s, to_ticks(ticks), recursive);
}

int bl_impl_sem_give(void *sem, int recursive)
{
    SemaphoreHandle_t s = (SemaphoreHandle_t)sem;
    if (!s) {
        return 0;
    }
    return (recursive ? xSemaphoreGiveRecursive(s) : xSemaphoreGive(s)) == pdTRUE;
}

int bl_impl_sem_count(void *sem)
{
    return sem ? uxSemaphoreGetCount((SemaphoreHandle_t)sem) : 0;
}

void bl_impl_sem_delete(void *sem)
{
    if (sem) {
        vSemaphoreDelete((SemaphoreHandle_t)sem);
    }
}

/* ---- queues ---- */

void *bl_impl_queue_create(uint32_t length, uint32_t item_size)
{
    return length && item_size ? xQueueCreate(length, item_size) : NULL;
}

static int queue_send_now(QueueHandle_t q, const void *item, TickType_t ticks, int where)
{
    switch (where) {
    case BL_QUEUE_FRONT:     return xQueueSendToFront(q, item, ticks) == pdTRUE;
    case BL_QUEUE_OVERWRITE: return xQueueOverwrite(q, item) == pdTRUE;
    default:                 return xQueueSendToBack(q, item, ticks) == pdTRUE;
    }
}

int bl_impl_queue_send(void *queue, const void *item, uint32_t ticks, int where)
{
    QueueHandle_t q = (QueueHandle_t)queue;
    if (!q || !item) {
        return 0;
    }
    if (!long_wait(ticks)) {
        return queue_send_now(q, item, to_ticks(ticks), where);
    }
    if (queue_send_now(q, item, 0, where)) {
        return 1;
    }
    WdtPause p;
    return queue_send_now(q, item, to_ticks(ticks), where);
}

static int queue_receive_now(QueueHandle_t q, void *item, TickType_t ticks, int peek)
{
    return (peek ? xQueuePeek(q, item, ticks) : xQueueReceive(q, item, ticks)) == pdTRUE;
}

int bl_impl_queue_receive(void *queue, void *item, uint32_t ticks, int peek)
{
    QueueHandle_t q = (QueueHandle_t)queue;
    if (!q || !item) {
        return 0;
    }
    if (!long_wait(ticks)) {
        return queue_receive_now(q, item, to_ticks(ticks), peek);
    }
    if (queue_receive_now(q, item, 0, peek)) {
        return 1;
    }
    WdtPause p;
    return queue_receive_now(q, item, to_ticks(ticks), peek);
}

int bl_impl_queue_count(void *queue, int spaces)
{
    QueueHandle_t q = (QueueHandle_t)queue;
    if (!q) {
        return 0;
    }
    return spaces ? uxQueueSpacesAvailable(q) : uxQueueMessagesWaiting(q);
}

int bl_impl_queue_reset(void *queue)
{
    if (queue) {
        xQueueReset((QueueHandle_t)queue);
    }
    return 1;
}

void bl_impl_queue_delete(void *queue)
{
    if (queue) {
        vQueueDelete((QueueHandle_t)queue);
    }
}
