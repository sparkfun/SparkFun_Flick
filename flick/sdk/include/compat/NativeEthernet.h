#pragma once
#include "baseline.h"
#if !defined(BL_HAS_ETHERNET)
#error "This board has no Ethernet in its Baseline (the Teensy 4.1 has it)."
#endif
#include "baseline_ethernet.h"   /* Ethernet (Teensy 4.1): the network runs in Baseline */
