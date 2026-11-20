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

static void note_sketch_task(void)
{
    if (!bl_sdk_current_task) {
        bl_sdk_sketch_task = bl_task_current();
        bl_sdk_current_task = current_task;
    }
}

static BaseType_t created_task(void *t, TaskHandle_t *created)
{
    if (created) {
        *created = t;
    }
    return t ? pdPASS : pdFAIL;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack_bytes, void *arg,
                                   UBaseType_t priority, TaskHandle_t *created, BaseType_t core)
{
    note_sketch_task();
    return created_task(bl_task_create(fn, name, stack_bytes, arg, (int)priority, core == tskNO_AFFINITY ? -1 : (int)core),
                        created);
}

BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t fn, const char *name, uint32_t stack_bytes, void *arg,
                                           UBaseType_t priority, TaskHandle_t *created, BaseType_t core,
                                           UBaseType_t caps)
{
    note_sketch_task();
    return created_task(bl_task_create_caps(fn, name, stack_bytes, arg, (int)priority,
                                            core == tskNO_AFFINITY ? -1 : (int)core, (uint32_t)caps),
                        created);
}

#endif /* BL_HAS_RTOS */
