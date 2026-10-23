/* bl_update.h - updating Baseline and the sketch over the air (bl_update.cpp). */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Confirm a new, not yet confirmed Baseline (call before deliberate restarts). */
void bl_update_confirm(void);

/* BL_UPDATE_DUAL, BL_UPDATE_STAGED or BL_UPDATE_NONE (baseline_types.h). */
int bl_update_mode(void);
const char *bl_update_mode_name(void);

/* At boot, after the loader has started and before the sketch: confirms a
 * new Baseline, finishes a staged update, and runs update mode. Returns true
 * if the sketch must not start this boot. */
bool bl_update_boot(void);

/* Baseline push from bsk (loader commands UBEGIN/UDATA/UEND). kind is "app"
 * (the raw app image, for two-slot boards) or "bsu" (the package, for staged
 * boards). Each returns NULL or an error text. bl_update_push_begin sets
 * *restart when the board must first restart into update mode (the reply
 * says so, and bsk sends again). After a successful end the caller restarts. */
const char *bl_update_push_begin(const char *kind, uint32_t size, const uint8_t sha256[32], bool via_wifi,
                                 bool *restart);
const char *bl_update_push_data(const uint8_t *data, size_t len);
const char *bl_update_push_end(void);
void bl_update_push_abort(void);

#ifdef __cplusplus
}
#endif
