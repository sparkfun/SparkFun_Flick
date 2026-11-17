// 04_usb_host_keyboard.ino - type on a USB keyboard plugged into the Teensy's USB host port.
// Prints each key, moves of a USB mouse, and what is plugged in. Teensy 4.1: the
// host header; MicroMod Teensy: the carrier's USB host port; Teensy 4.0: the
// pads underneath. The standard USBHost_t36 library: its examples work the same way.
#if !defined(BL_HAS_USB_HOST)
#error "This board has no USB host port."
#endif
#include <USBHost_t36.h>

USBHost myusb;
USBHub hub1(myusb);
KeyboardController keyboard1(myusb);
MouseController mouse1(myusb);

bool wasKeyboard = false, wasMouse = false;

void onPress(int key)
{
    if (key >= 32 && key < 127) {
        Serial.printf("key '%c'", key);
    } else {
        Serial.printf("key 0x%02X", key);
    }
    Serial.printf("  (code %u, modifiers 0x%02X)\n", keyboard1.getOemKey(), keyboard1.getModifiers());
}

void setup()
{
    Serial.begin(115200);
    myusb.begin();
    keyboard1.attachPress(onPress);
    Serial.println("Plug a keyboard or mouse into the USB host port.");
}

void loop()
{
    myusb.Task();

    if ((bool)keyboard1 != wasKeyboard) {
        wasKeyboard = keyboard1;
        if (wasKeyboard) {
            Serial.printf("Keyboard: %s %s (%04X:%04X)\n", keyboard1.manufacturer(), keyboard1.product(),
                          keyboard1.idVendor(), keyboard1.idProduct());
        } else {
            Serial.println("Keyboard unplugged");
        }
    }
    if ((bool)mouse1 != wasMouse) {
        wasMouse = mouse1;
        Serial.println(wasMouse ? "Mouse plugged in" : "Mouse unplugged");
    }
    if (mouse1.available()) {
        Serial.printf("mouse: buttons %u, x %d, y %d, wheel %d\n", mouse1.getButtons(), mouse1.getMouseX(),
                      mouse1.getMouseY(), mouse1.getWheel());
        mouse1.mouseDataClear();
    }
    delay(10);
}
