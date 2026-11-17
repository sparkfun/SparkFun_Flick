#pragma once
/* The WiFi/BLE co-processor of a board that has one (an ESP32-P4 with an
 * ESP32-C5 or C6 beside it, over ESP-Hosted). On other boards these do
 * nothing: bl_hosted_ready() is true and bl_hosted_info() is NULL. */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* At boot: bring the link up in the background (Arduino would otherwise do it
 * inside the sketch's first WiFi or BLE call). */
void bl_hosted_start(void);

/* Before a WiFi or BLE call that starts the radio: waits for the boot-time
 * start (with the task watchdog paused) and says whether the co-processor
 * answered. A co-processor that doesn't answer then costs the sketch an error,
 * not seconds of retries and a watchdog reset. */
bool bl_hosted_ready(void);

/* For INFO: "esp32c5 2.12.11", "starting" or "not answering"; NULL without a
 * co-processor. */
const char *bl_hosted_info(void);

#ifdef __cplusplus
}
#endif
