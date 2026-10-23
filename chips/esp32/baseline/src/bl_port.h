/*
 * bl_port.h - Baseline port layer: classic ESP32. Every chip supplies this
 * header and bl_port_mmu.c; the rest of Baseline is shared. Board defaults
 * (Serial1/Serial2 pins, microSD wiring) are in the target's bl_board.h.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "bl_target.h"     // + the target's bl_board.h

/* Console + sketch upload run on UART0 (the board's USB-serial chip) at 921600.
 * 2 Mbaud was tried (2026-10-22/23): on the IoT RedBoard's CH340 about 3% of
 * console replies lost a byte (0 of 120 at 921600), and uploads were no faster,
 * being bound by request/reply round trips. A board may set BL_PORT_CONSOLE_BAUD
 * in its bl_board.h. The ROM and bootloader messages stay at 115200
 * (chips/esp32/idf/sdkconfig.defaults says why). bsk tries 921600,
 * 2000000 (Baselines built in between), then 115200 (older Baselines). */
#define BL_PORT_CONSOLE_USB 0
#ifndef BL_PORT_CONSOLE_BAUD
#define BL_PORT_CONSOLE_BAUD 921600
#endif

/* Flash and PSRAM pins, from eFuse and the chip package, read once
 * (bl_port_pins.c): bit n set = GPIOn is taken. */
#ifdef __cplusplus
extern "C" {
#endif
uint64_t bl_port_memory_pins(void);
#ifdef __cplusplus
}
#endif

/* Pins a sketch may not touch. */
static inline bool bl_pin_usable(int pin)
{
    if (!GPIO_IS_VALID_GPIO(pin)) {
        return false;
    }
    if (pin == 1 || pin == 3) {
        return false;   // UART0: console and sketch upload
    }
    if (bl_port_memory_pins() & (1ULL << pin)) {
        return false;   // flash or PSRAM (6-11 on most modules; see bl_port_pins.c)
    }
    return true;
}

static inline bool bl_pin_is_adc(int pin)
{
    // ADC1: 32-39. ADC2 (not usable while WiFi is on): 0, 2, 4, 12-15, 25-27.
    return (pin >= 32 && pin <= 39) || pin == 0 || pin == 2 || pin == 4 ||
           (pin >= 12 && pin <= 15) || (pin >= 25 && pin <= 27);
}

#ifdef __cplusplus
extern "C" {
#endif

/* Map `len` bytes of flash at `paddr` at the fixed sketch windows
 * (BL_SKETCH_IBUS_BASE / BL_SKETCH_DBUS_BASE). */
void bl_port_map_window(uint32_t paddr, uint32_t len);
/* True if something else already uses the sketch window. */
bool bl_port_window_in_use(void);

/* rgbLedWrite() and LED strips for this target, bit-banged (bl_port_rgb.c):
 * the RMT driver's ISRs need ~1.9 KB of IRAM that the classic ESP32 does not
 * have once the PSRAM driver is in. timing = { t0h, t0l, t1h, t1l } in 0.1 us. */
#define BL_PORT_RGB_WRITE 1
#define BL_PORT_LED_STRIP 1
void bl_port_rgb_write(int pin, uint8_t red, uint8_t green, uint8_t blue);
bool bl_port_led_strip_write(int pin, const uint8_t *data, size_t len, const uint8_t timing[4]);

#ifdef __cplusplus
}
#endif
