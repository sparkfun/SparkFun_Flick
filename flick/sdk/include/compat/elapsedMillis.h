#pragma once
#include "baseline.h"     /* elapsedMillis is in baseline_teensy.h (Teensy 4.x) */
#if !defined(BL_PLATFORM_TEENSY)
#error "This board has no elapsedMillis.h (it is a Teensy API)."
#endif
