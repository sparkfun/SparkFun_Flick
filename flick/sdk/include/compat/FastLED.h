/* Compatibility header: classic Arduino / ESP-IDF code includes this name.
 * Everything it needs comes from baseline.h and runs through Baseline. */
#pragma once
#include "baseline.h"
#if !defined(BL_HAS_LED_STRIP)
#error "This board has no WS2812 LED support in its Baseline."
#endif
