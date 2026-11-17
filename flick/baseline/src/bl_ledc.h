#pragma once
/* LEDC duty updates without long interrupt-off spins.
 *
 * On the classic ESP32, ledc_update_duty() holds the LEDC spinlock (interrupts
 * off on that core) while it waits for the previous duty change to take
 * effect, which happens at the start of the next PWM period: up to 20 ms for a
 * 50 Hz servo. That starves every other interrupt on the core; at 921600 baud
 * the console's 128-byte UART FIFO overflows in under 1.5 ms, so a sketch that
 * updates a servo often made uploads fail. bl_ledc_settle() waits for the
 * hardware outside the lock, sleeping a tick at a time, so the spin inside
 * ledcWrite() finds the update already done. Later chips don't spin. */
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32
#include "esp32-hal-periman.h"
#include "esp32-hal-ledc.h"
#include "soc/soc_caps.h"
#include "soc/ledc_struct.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static inline void bl_ledc_settle(int pin)
{
    ledc_channel_handle_t *bus = (ledc_channel_handle_t *)perimanGetPinBus((uint8_t)pin, ESP32_BUS_TYPE_LEDC);
    if (!bus) {
        return;     // not attached yet: nothing pending
    }
    uint8_t group = bus->channel / SOC_LEDC_CHANNEL_NUM, channel = bus->channel % SOC_LEDC_CHANNEL_NUM;
    // Bounded by one period of the slowest PWM worth waiting for (1 Hz).
    for (int i = 0; i < 1100 && LEDC.channel_group[group].channel[channel].conf1.duty_start; i++) {
        vTaskDelay(1);
    }
}
#else
static inline void bl_ledc_settle(int pin)
{
    (void)pin;
}
#endif
