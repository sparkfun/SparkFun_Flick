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
    bl_usb_boot(bl_state_safe_mode_reason() != NULL);     // USB device mode, if the sketch asked for it

    BLOG_I("Baseline %s, ABI %d.%d, %s (%s), reset reason %d, free heap %u",
           BASELINE_VERSION, BL_ABI_MAJOR, BL_ABI_MINOR, BL_TARGET_CHIP_NAME, BL_BOARD_NAME, (int)esp_reset_reason(),
           (unsigned)esp_get_free_heap_size());

    loader_start();

    const char *safe = bl_state_safe_mode_reason();
    if (safe) {
        BLOG_W("SAFE MODE (%s): sketch not started. Upload a sketch or reset to retry.", safe);
        return;
    }
    sketch_start(bl_state_active_slot());
}
