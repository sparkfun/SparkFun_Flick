#pragma once

#define BASELINE_VERSION "0.3.0"

#ifdef __cplusplus
extern "C" {
#endif

void bl_main(void);
void bl_last_crash_clear(void);     /* bl_impl_extras.cpp: erase the recorded coredump */

#ifdef __cplusplus
}
#endif
