/*
 * bl_port.h - Baseline port layer: ESP32-C6. Every chip supplies this
 * header and bl_port_mmu.c; the rest of Baseline is shared. Board defaults
 * (Serial1/Serial2 pins, microSD wiring) are in the target's bl_board.h.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "bl_target.h"     // + the target's bl_board.h

/* Console + sketch upload run on the native USB port (Arduino HWCDC Serial). */
#define BL_PORT_CONSOLE_USB 1

/* Pins a sketch may not touch. */
static inline bool bl_pin_usable(int pin)
{
    if (!GPIO_IS_VALID_GPIO(pin)) {
        return false;
    }
    if (pin >= 24 && pin <= 30) {
        return false;   // SPI flash (27 is its supply on some packages)
    }
    if (pin == 12 || pin == 13) {
        return false;   // native USB: console and sketch upload
    }
    return true;
}

static inline bool bl_pin_is_adc(int pin)
{
    return (pin >= 0 && pin <= 6) && bl_pin_usable(pin);    // ADC1: 0-6
}

#ifdef __cplusplus
extern "C" {
#endif

/* Map `len` bytes of flash at `paddr` at the fixed sketch windows
 * (BL_SKETCH_IBUS_BASE / BL_SKETCH_DBUS_BASE). */
void bl_port_map_window(uint32_t paddr, uint32_t len);
/* True if something else already uses the sketch window. */
bool bl_port_window_in_use(void);

#ifdef __cplusplus
}
#endif
