/* sketch_usb.cpp - USB device mode (see baseline_usb.h). */
#include "baseline.h"

ESPUSB USB;

static uint32_t s_wanted;

void bl_usb_want(uint32_t device)
{
    s_wanted |= device;
}

bool ESPUSB::begin()
{
    if (!s_wanted) {
        return true;    // nothing but the serial port, which is always there
    }
    // Returns at once if Baseline already runs these devices; otherwise it
    // saves them and reboots, and this sketch starts again.
    return bl_usb_begin(s_wanted) == BL_OK;
}
