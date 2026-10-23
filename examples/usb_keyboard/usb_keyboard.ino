// usb_keyboard.ino - the board becomes a USB keyboard and media remote (ESP32-S3).
// Press BOOT: it types a line of text. Hold BOOT for a second: volume up.
// The first run reboots once into USB device mode; Serial keeps working.
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDConsumerControl.h>

USBHIDKeyboard Keyboard;
USBHIDConsumerControl Media;
const int BUTTON = 0;   // BOOT button

void setup()
{
    Serial.begin(115200);
    pinMode(BUTTON, INPUT_PULLUP);
    Keyboard.begin();
    Media.begin();
    if (!USB.begin()) {
        Serial.println("USB device mode needs an ESP32-S3");
    }
}

void loop()
{
    if (digitalRead(BUTTON) == LOW) {
        uint32_t start = millis();
        while (digitalRead(BUTTON) == LOW) {
            delay(10);
        }
        if (millis() - start > 1000) {
            Media.press(CONSUMER_CONTROL_VOLUME_INCREMENT);
            Media.release();
        } else {
            Keyboard.println("Hello from Flick!");
        }
    }
    delay(10);
}
