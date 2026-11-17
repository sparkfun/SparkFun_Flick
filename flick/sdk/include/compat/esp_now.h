/* Compatibility header: classic Arduino / ESP-IDF code includes this name.
 * Everything it needs comes from baseline.h and runs through Baseline. */
#pragma once
#include "baseline.h"
#if !defined(BL_HAS_ESPNOW)
#error "This board has no ESP-NOW (no WiFi radio, or WiFi through a co-processor without it)."
#endif
