/* Compatibility header: classic Arduino / ESP-IDF code includes this name.
 * Baseline owns the app slots: a sketch installs firmware with Update
 * (<Update.h>), and lists the partition table with esp_partition_find. */
#pragma once
#include "baseline.h"
#if !defined(BL_PLATFORM_ESP32)
#error "This board has no esp_ota_ops.h (it is an ESP32 API)."
#endif
