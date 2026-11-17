/* API table: USB device mode (ABI 1.6 usb_*), on the Teensy Baselines built as
 * Serial + Keyboard + Mouse + Joystick (the *_hid targets, Teensyduino's
 * USB_SERIAL_HID). The SDK's ESP32-S3 classes (USBHIDKeyboard, USBHIDMouse,
 * USBHIDGamepad, USBHIDConsumerControl) drive Teensyduino's Keyboard, Mouse and
 * Joystick through these. Other Teensy Baselines: the weak stubs. */
#include "bl_teensy.h"

#if defined(USB_SERIAL_HID)

#define DEVICES (BL_USB_KEYBOARD | BL_USB_MOUSE | BL_USB_GAMEPAD | BL_USB_CONSUMER)

extern volatile uint8_t usb_configuration;

extern "C" int bl_impl_usb_begin(uint32_t devices)
{
    return (devices & ~DEVICES) ? BL_ERR_UNSUPPORTED : BL_OK;   /* always running: nothing to switch */
}

extern "C" void bl_impl_usb_end(void)
{
}

extern "C" uint32_t bl_impl_usb_devices(void)
{
    return DEVICES;
}

extern "C" int bl_impl_usb_mounted(void)
{
    return usb_configuration ? 1 : 0;
}

/* Arduino's key codes (Keyboard.h, as the ESP32-S3 uses them) to Teensyduino's. */
static uint16_t teensy_key(uint32_t key)
{
    if (key < 0x80) {
        return (uint16_t)key;                   /* ASCII: Teensyduino maps it itself */
    }
    switch (key) {
    case 0x80: return MODIFIERKEY_LEFT_CTRL;
    case 0x81: return MODIFIERKEY_LEFT_SHIFT;
    case 0x82: return MODIFIERKEY_LEFT_ALT;
    case 0x83: return MODIFIERKEY_LEFT_GUI;
    case 0x84: return MODIFIERKEY_RIGHT_CTRL;
    case 0x85: return MODIFIERKEY_RIGHT_SHIFT;
    case 0x86: return MODIFIERKEY_RIGHT_ALT;
    case 0x87: return MODIFIERKEY_RIGHT_GUI;
    case 0xB0: return KEY_ENTER;
    case 0xB1: return KEY_ESC;
    case 0xB2: return KEY_BACKSPACE;
    case 0xB3: return KEY_TAB;
    case 0xC1: return KEY_CAPS_LOCK;
    case 0xD1: return KEY_INSERT;
    case 0xD2: return KEY_HOME;
    case 0xD3: return KEY_PAGE_UP;
    case 0xD4: return KEY_DELETE;
    case 0xD5: return KEY_END;
    case 0xD6: return KEY_PAGE_DOWN;
    case 0xD7: return KEY_RIGHT;
    case 0xD8: return KEY_LEFT;
    case 0xD9: return KEY_DOWN;
    case 0xDA: return KEY_UP;
    default:
        if (key >= 0xC2 && key <= 0xCD) {
            return KEY_F1 + (uint16_t)(key - 0xC2);     /* F1-F12 */
        }
        return 0;
    }
}

extern "C" int bl_impl_usb_keyboard(int op, uint32_t key)
{
    uint16_t k = op == BL_USB_RELEASE_ALL ? 0 : teensy_key(key);
    switch (op) {
    case BL_USB_PRESS:       if (!k) return 0; Keyboard.press(k); return 1;
    case BL_USB_RELEASE:     if (!k) return 0; Keyboard.release(k); return 1;
    case BL_USB_RELEASE_ALL: Keyboard.releaseAll(); return 1;
    case BL_USB_WRITE:       if (!k) return 0; Keyboard.press(k); Keyboard.release(k); return 1;
    default:                 return 0;
    }
}

extern "C" int bl_impl_usb_mouse(int op, int x, int y, int wheel, int buttons)
{
    switch (op) {
    case BL_USB_MOVE:        Mouse.move(x, y, wheel); return 1;
    case BL_USB_PRESS:       Mouse.press((uint8_t)buttons); return 1;
    case BL_USB_RELEASE:     Mouse.release((uint8_t)buttons); return 1;
    case BL_USB_CLICK:       Mouse.click((uint8_t)buttons); return 1;
    default:                 return 0;
    }
}

/* The ESP32-S3 gamepad's -127..127 axes and 8-way hat (0 = centered, 1 = up,
 * then clockwise) to Teensyduino's 0..1023 axes and hat angle. */
static int axis(int v)
{
    v = v < -127 ? -127 : v > 127 ? 127 : v;
    return (v + 127) * 1023 / 254;
}

extern "C" int bl_impl_usb_gamepad(int x, int y, int z, int rz, int rx, int ry, int hat, uint32_t buttons)
{
    Joystick.useManualSend(true);
    Joystick.X(axis(x));
    Joystick.Y(axis(y));
    Joystick.Z(axis(z));
    Joystick.Zrotate(axis(rz));
    Joystick.sliderLeft(axis(rx));
    Joystick.sliderRight(axis(ry));
    Joystick.hat(hat >= 1 && hat <= 8 ? (hat - 1) * 45 : -1);
    for (int b = 0; b < 32; b++) {
        Joystick.button(b + 1, (buttons >> b) & 1);
    }
    Joystick.send_now();
    return 1;
}

static uint16_t s_media;

extern "C" int bl_impl_usb_consumer(int op, uint16_t usage)
{
    if (op == BL_USB_PRESS && usage) {
        s_media = 0xE400 | (usage & 0x3FF);     /* Teensyduino's KEY_MEDIA_* encoding */
        Keyboard.press(s_media);
        return 1;
    }
    if (op == BL_USB_RELEASE && s_media) {
        Keyboard.release(s_media);
        s_media = 0;
        return 1;
    }
    return 0;
}

#endif /* USB_SERIAL_HID */
