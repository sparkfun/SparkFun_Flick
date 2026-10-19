/* Pin rules for sketches on the SparkFun ESP32-S3 Thing Plus. */
#pragma once

#include <stdbool.h>
#include "driver/gpio.h"

/* Pins a sketch may not touch. */
static inline bool bl_pin_usable(int pin)
{
    if (!GPIO_IS_VALID_GPIO(pin)) {
        return false;
    }
    if (pin >= 26 && pin <= 32) {
        return false;   // SPI flash + PSRAM
    }
    if (pin == 19 || pin == 20) {
        return false;   // native USB: console and sketch upload
    }
    return true;
}

static inline bool bl_pin_is_adc(int pin)
{
    return pin >= 1 && pin <= 20 && bl_pin_usable(pin);    // ADC1: 1-10, ADC2: 11-20
}
