/* Compatibility header: ESP-IDF / Arduino-ESP32 code includes this name.
 * FreeRTOS's names come from baseline_rtos.h and run through Baseline. */
#pragma once
#include "baseline.h"
#if !defined(BL_HAS_RTOS)
#error "This board has no FreeRTOS (it is an ESP32 API)."
#endif
