// 06_deep_sleep.ino - deep sleep on a Teensy 4.x with the Snooze library's names.
// The board sleeps for 5 s (the USB port goes away meanwhile), then the sketch
// starts again; a counter in RTC memory survives. Wake on a pin instead with
// digital.pinMode(2, INPUT_PULLUP, FALLING) and a button from pin 2 to GND.
#if !defined(BL_PLATFORM_TEENSY)
#error "This board has no Snooze (on ESP32 boards see 06_power_and_usb/01_deep_sleep)."
#endif
#include <Snooze.h>

SnoozeTimer timer;
SnoozeDigital digital;
SnoozeBlock config(timer, digital);

void setup()
{
    Serial.begin(115200);
    uint32_t size;
    uint32_t *wakes = (uint32_t *)bl_rtc_memory(&size);
    if (bl_sleep_wake_cause() == 0) {
        *wakes = 0;                 // a power-on or reset, not a wake
    }
    delay(2000);                    // time for the serial monitor to reconnect
    Serial.printf("Awake (wake %lu). Sleeping for 5 s...\n", (unsigned long)(*wakes)++);
    timer.setTimer(5);              // seconds
    Snooze.deepSleep(config);       // the sketch starts again when the board wakes
}

void loop()
{
}
