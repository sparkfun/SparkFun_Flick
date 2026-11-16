/* Compatibility header: classic Arduino / ESP-IDF code includes this name.
 * Everything it needs comes from baseline.h and runs through Baseline. */
#pragma once
#include "baseline.h"
#if !defined(BL_HAS_USB_DEVICE)
#error "This board has no USB device mode in this Baseline (the ESP32-S3 has it)."
#endif
