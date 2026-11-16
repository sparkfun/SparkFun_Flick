/* Compatibility header: classic Arduino / ESP-IDF code includes this name.
 * Everything it needs comes from baseline.h and runs through Baseline. */
#pragma once
#include "baseline.h"
#if !defined(BL_HAS_BT_CLASSIC)
#error "This board has no Bluetooth Classic (only the classic ESP32 has it)."
#endif
