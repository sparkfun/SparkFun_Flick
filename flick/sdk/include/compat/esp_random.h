/* Compatibility header: classic Arduino / ESP-IDF code includes this name.
 * Everything it needs comes from baseline.h and runs through Baseline. */
#pragma once
#include "baseline.h"
#if !defined(BL_PLATFORM_ESP32)
#error "This board has no esp_random.h (it is an ESP32 API)."
#endif
