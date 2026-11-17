/* Compatibility header: classic Arduino / ESP-IDF code includes this name.
 * Everything it needs comes from baseline.h and runs through Baseline. */
#pragma once
#include "baseline.h"
#if !defined(BL_PLATFORM_ESP32) && !defined(BL_PLATFORM_RP2)
#error "This board has no FastLED support in this Baseline yet (the ESP32 family and the RP2040/RP2350 have it)."
#endif
