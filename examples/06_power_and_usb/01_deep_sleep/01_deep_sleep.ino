// 01_deep_sleep.ino - sleep for 10 seconds (or until BOOT is pressed), wake, repeat.
// The boot count is kept in RTC memory, which survives deep sleep.
// Note: the USB serial port disappears while the board sleeps.
#include <esp_sleep.h>

struct Saved {
    uint32_t boots;
};
Saved &saved = *(Saved *)bl_rtc_memory(nullptr);

void setup()
{
    Serial.begin(115200);
    delay(500);     // time for the serial monitor to reconnect
    saved.boots++;
    Serial.printf("Boot #%u, woke up because: ", (unsigned)saved.boots);
    switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("timer"); break;
    case ESP_SLEEP_WAKEUP_EXT0:  Serial.println("BOOT button"); break;
    default:                     Serial.println("power-on or reset"); break;
    }

    esp_sleep_enable_timer_wakeup(10 * 1000000ULL);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);    // BOOT button pulls GPIO0 low
    Serial.println("Sleeping for 10 s (press BOOT to wake early)...");
    esp_deep_sleep_start();
}

void loop()
{
}
