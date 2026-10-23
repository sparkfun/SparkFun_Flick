#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_partition.h"
#include "sketch_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

const esp_partition_t *sketch_slot_partition(int slot);

/* Validate the image in partition p (header, bounds, SHA-256).
 * Returns NULL if good (and fills *out), else a reason string. */
const char *sketch_check(const esp_partition_t *p, bl_sketch_header_t *out);

/* Validate, map, and start the sketch in `slot` (0/1) on its own task. */
esp_err_t sketch_start(int slot);

bool        sketch_is_current_task(void);

/* Baseline calls that can block longer than the task watchdog timeout (5 s)
 * take the sketch task off the watchdog while they run. */
bool        sketch_wdt_pause(void);             /* returns whether it paused */
void        sketch_wdt_resume(bool paused);

#ifdef __cplusplus
}
/* RAII form for C++ implementations. */
struct WdtPause {
    bool paused = sketch_wdt_pause();
    ~WdtPause() { sketch_wdt_resume(paused); }
};
extern "C" {
#endif
const char *sketch_status(void);
void        sketch_set_status(const char *status);     /* e.g. "update mode" when no sketch runs */
const char *sketch_name(void);

#ifdef __cplusplus
}
#endif
