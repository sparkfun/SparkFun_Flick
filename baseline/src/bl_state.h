/* Boot state: active sketch slot, trial/rollback, crash counter, safe mode. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define BL_CRASH_LIMIT          3       /* consecutive sketch crashes before safe mode */
#define BL_STABLE_AFTER_MS      10000   /* sketch counts as good after running this long */
#define BL_SAFE_MODE_PIN        0       /* BOOT button: hold at reset for safe mode */

void        bl_state_init(void);
const char *bl_state_safe_mode_reason(void);    /* NULL when not in safe mode */
void        bl_state_request_safe_mode(void);   /* applies on next reboot */
int         bl_state_active_slot(void);         /* 0 = sketch_a, 1 = sketch_b */
void        bl_state_set_pending(int slot);     /* new sketch: activate on trial */
void        bl_state_sketch_running(void);      /* sketch code is about to run */
void        bl_state_sketch_stable(void);       /* sketch ran BL_STABLE_AFTER_MS */

#ifdef __cplusplus
}
#endif
