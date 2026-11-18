// 01_deep_sleep.ino - stay awake 5 s, sleep 10 s, wake, repeat.
// The boot count is kept in RTC memory, which survives deep sleep.
//
// The USB serial port disappears while the board sleeps. Uploads work during the
// 5 s awake; `flick upload` also waits for a sleeping board and catches it when it
// wakes. The BOOT button wakes the board early only on chips whose BOOT pin can
// wake them from deep sleep (the ESP32 and S3); on the C3, C5 and C6 it can't, and
// only the timer wakes them.
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
    // BOOT pulls its pin low. Only an RTC pin can wake the chip from deep sleep.
    bool boot_wakes = esp_sleep_enable_ext0_wakeup((gpio_num_t)BL_TARGET_BOOT_PIN, 0) == ESP_OK;

    for (int i = 5; i > 0; i--) {
        Serial.printf("Sleeping in %d s\n", i);
        delay(1000);
    }
    Serial.println(boot_wakes ? "Sleeping for 10 s (press BOOT to wake early)..."
                              : "Sleeping for 10 s (BOOT can't wake this chip: wait for the timer)...");
    Serial.flush();     // send the last line before the USB port goes away
    esp_deep_sleep_start();
}

void loop()
{
}
