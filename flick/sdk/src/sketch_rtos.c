/*
 * sketch_rtos.c - xTaskCreatePinnedToCore for sketches (baseline_rtos.h).
 *
 * Callbacks (pumps, sketch_crt.c) must only run on the sketch's own task. The
 * first task a sketch creates is always created from that task (nothing else
 * runs sketch code yet), so that is where it is recorded.
 */
#include "baseline.h"

#if defined(BL_HAS_RTOS)

extern void *(*bl_sdk_current_task)(void);
extern void *bl_sdk_sketch_task;

static void *current_task(void)
{
    return bl_task_current();
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack_bytes, void *arg,
                                   UBaseType_t priority, TaskHandle_t *created, BaseType_t core)
{
    if (!bl_sdk_current_task) {
        bl_sdk_sketch_task = bl_task_current();
        bl_sdk_current_task = current_task;
    }
    void *t = bl_task_create(fn, name, stack_bytes, arg, (int)priority, core == tskNO_AFFINITY ? -1 : (int)core);
    if (created) {
        *created = t;
    }
    return t ? pdPASS : pdFAIL;
}

#endif /* BL_HAS_RTOS */
