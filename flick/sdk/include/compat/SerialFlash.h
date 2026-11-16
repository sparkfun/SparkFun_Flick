#pragma once
/* SerialFlash: the Audio library's players for it are not in this Baseline yet.
 * The Audio System Design Tool adds #include <SerialFlash.h> to every sketch,
 * so the header exists. */
#include "baseline.h"
#if !defined(BL_HAS_AUDIO)
#error "This board has no Teensy Audio library (SerialFlash.h is for Teensy 4.x)."
#endif
