#include <Arduino.h>
#include "bl_main.h"
#include "bl_console.h"
#include "bl_state.h"
#include "loader_proto.h"
#include "sketch_loader.h"
#include "gen/bl_impl.h"

void bl_main(void)
{
    bl_console_init();
    bl_state_init();

    BLOG_I("Baseline %s, ABI %d.%d, ESP32-S3, reset reason %d, free heap %u",
           BASELINE_VERSION, BL_ABI_MAJOR, BL_ABI_MINOR, (int)esp_reset_reason(),
           (unsigned)esp_get_free_heap_size());

    loader_start();

    const char *safe = bl_state_safe_mode_reason();
    if (safe) {
        BLOG_W("SAFE MODE (%s): sketch not started. Upload a sketch or reset to retry.", safe);
        return;
    }
    sketch_start(bl_state_active_slot());
}
