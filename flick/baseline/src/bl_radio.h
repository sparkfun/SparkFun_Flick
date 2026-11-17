#pragma once
/* Which radios this Baseline has: the chip's own, or a co-processor's over
 * ESP-Hosted (an ESP32-P4 with an ESP32-C5 or C6 beside it). The same
 * conditions as Arduino-ESP32's WiFi and BLE libraries. Without them, the
 * WiFi, ESP-NOW and BLE table entries are the generated stubs. */
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#if SOC_WIFI_SUPPORTED || CONFIG_ESP_HOSTED_ENABLED
#define BL_RADIO_WIFI 1
#else
#define BL_RADIO_WIFI 0
#endif

/* ESP-NOW: only on the chip's own radio (ESP-Hosted doesn't carry it). */
#if SOC_WIFI_SUPPORTED
#define BL_RADIO_ESPNOW 1
#else
#define BL_RADIO_ESPNOW 0
#endif

#if SOC_BT_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE
#define BL_RADIO_BLE 1
#else
#define BL_RADIO_BLE 0
#endif
