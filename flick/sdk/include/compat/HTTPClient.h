/* Compatibility header: classic Arduino / ESP-IDF code includes this name.
 * Everything it needs comes from baseline.h and runs through Baseline. */
#pragma once
#include "baseline.h"
#if !defined(BL_HAS_NET)
#error "This board has no network (WiFi, or Ethernet on a Teensy 4.1)."
#endif
