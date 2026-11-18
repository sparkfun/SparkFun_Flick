// 03_button_interrupt.ino - count button presses with attachInterrupt.
// The handler runs on the sketch task (Baseline defers it), so Serial is safe here.
//
// The button is the board's BOOT button: GPIO0 on the ESP32 and S3, GPIO9 on the C3
// and C6, BOOTSEL on RP2040/RP2350 boards (Baseline reads it every 20 ms). A Teensy's
// only button starts its bootloader, so there connect a button from pin 2 to GND.

#if BL_TARGET_BOOT_PIN >= 0
const int BUTTON = BL_TARGET_BOOT_PIN;
#define BUTTON_NAME "the BOOT button"
#else
const int BUTTON = 2;
#define BUTTON_NAME "a button between pin 2 and GND"
#endif
volatile int presses = 0;

void IRAM_ATTR onPress()
{
    presses++;
    Serial.printf("Button pressed (%d)\n", presses);
}

void setup()
{
    pinMode(BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON), onPress, FALLING);
    Serial.println("Press " BUTTON_NAME);
}

void loop()
{
    delay(100);
}
