/* Compatibility header: classic Arduino / ESP-IDF code includes this name.
 * Everything it needs comes from baseline.h and runs through Baseline. */
#pragma once
#include "baseline.h"
#if !defined(BL_HAS_TLS)
#error "This board has no TLS in its Baseline (the ESP32 family with WiFi has it)."
#endif
