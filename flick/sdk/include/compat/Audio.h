#pragma once
#include "baseline.h"
#if !defined(BL_HAS_AUDIO)
#error "This board has no Teensy Audio library (Audio.h is for Teensy 4.x)."
#endif
#include "baseline_audio.h"     /* the Teensy Audio library (Teensy 4.x): objects live in Baseline */
