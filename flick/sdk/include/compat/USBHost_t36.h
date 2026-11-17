#pragma once
#include "baseline.h"
#if !defined(BL_HAS_USB_HOST)
#error "This board has no USB host port in Baseline (USBHost_t36.h is for Teensy 4.x)."
#endif
#include "baseline_usbhost.h"   /* the USB host port (Teensy 4.x): the drivers live in Baseline */
