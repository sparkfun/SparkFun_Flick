/*
 * bl_port.h - Baseline port layer: ESP32-P4. Every chip supplies this
 * header and bl_port_mmu.c; the rest of Baseline is shared. Board defaults
 * (Serial1/Serial2 pins, microSD wiring) are in the target's bl_board.h.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "bl_target.h"     // + the target's bl_board.h

/* Console + sketch upload run on the native USB-Serial-JTAG port (Arduino
 * HWCDC Serial). The P4's flash and PSRAM have dedicated pins, not GPIOs. */
#define BL_PORT_CONSOLE_USB 1

/* Pins a sketch may not touch. */
static inline bool bl_pin_usable(int pin)
{
    if (!GPIO_IS_VALID_GPIO(pin)) {
        return false;
    }
    if (pin == 24 || pin == 25) {
        return false;   // USB-Serial-JTAG: console and sketch upload
    }
#if CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
    // The SDIO link to the WiFi/BLE co-processor, and its reset line.
    if (pin == CONFIG_ESP_HOSTED_SDIO_PIN_CLK || pin == CONFIG_ESP_HOSTED_SDIO_PIN_CMD ||
        pin == CONFIG_ESP_HOSTED_SDIO_PIN_D0 || pin == CONFIG_ESP_HOSTED_SDIO_PIN_D1 ||
        pin == CONFIG_ESP_HOSTED_SDIO_PIN_D2 || pin == CONFIG_ESP_HOSTED_SDIO_PIN_D3 ||
        pin == CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE) {
        return false;
    }
#endif
    return true;
}

static inline bool bl_pin_is_adc(int pin)
{
    // ADC1: GPIO 16-23; ADC2: GPIO 49-54 (the P4 has no radio of its own, so both work)
    return ((pin >= 16 && pin <= 23) || (pin >= 49 && pin <= 54)) && bl_pin_usable(pin);
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
