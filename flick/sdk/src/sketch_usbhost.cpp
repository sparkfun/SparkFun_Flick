/* USB host classes (baseline_usbhost.h): which Baseline driver each object
 * uses, and the pump that runs keyboard callbacks on the sketch's task. */
#include "baseline.h"
#if defined(BL_HAS_USB_HOST)
#include "baseline_usbhost.h"
#endif

#if defined(BL_HAS_USB_HOST)     /* the whole file: boards with this feature only */

static uint8_t s_claimed[BL_USBH_HUB + 1];

int bl_usbh_claim(int kind)
{
    return kind >= 0 && kind <= BL_USBH_HUB ? s_claimed[kind]++ : 0;
}

static KeyboardController *s_keyboards[2];

static void keyboard_pump(void)
{
    for (KeyboardController *k : s_keyboards) {
        if (k) {
            k->deliver();
        }
    }
}

KeyboardController::KeyboardController(USBHost &) : USBHostDevice(BL_USBH_KEYBOARD)
{
    if (idx_ < 2) {
        if (!s_keyboards[0] && !s_keyboards[1]) {
            bl_sketch_add_pump(keyboard_pump);
        }
        s_keyboards[idx_] = this;
    }
}

void KeyboardController::deliver()
{
    bl_usbh_key_t ev;
    while (bl_usbh_key_take(idx_, &ev) == 1) {
        key_ = ev.key;
        modifiers_ = ev.modifiers;
        oem_ = ev.oem;
        switch (ev.type) {
        case BL_USBH_KEY_PRESS:       if (press_) press_(ev.key); break;
        case BL_USBH_KEY_RELEASE:     if (release_) release_(ev.key); break;
        case BL_USBH_KEY_RAW_PRESS:   if (raw_press_) raw_press_((uint8_t)ev.key); break;
        case BL_USBH_KEY_RAW_RELEASE: if (raw_release_) raw_release_((uint8_t)ev.key); break;
        }
    }
}

#endif /* BL_HAS_USB_HOST */
