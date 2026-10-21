/*
 * bl_port.h - Baseline port layer: ESP32-S3 (SparkFun ESP32-S3 Thing Plus).
 * Every target supplies this header and bl_port_mmu.c; the rest of Baseline
 * is shared.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"

/* Console + sketch upload run on the native USB port (Arduino HWCDC Serial). */
#define BL_PORT_CONSOLE_USB 1

/* Default pins for Serial1 / Serial2 (-1: the sketch must pass pins). */
#define BL_SERIAL1_RX   44      // the pins labeled RX/TX on the Thing Plus
#define BL_SERIAL1_TX   43
#define BL_SERIAL2_RX   (-1)
#define BL_SERIAL2_TX   (-1)

/* microSD: the S3 Thing Plus wires its slot as 4-bit SDIO (SD_MMC). */
#define BL_SD_DEFAULT_BUS   1       // BL_SD_MMC
#define BL_SD_CS            (-1)    // no SPI wiring to the slot
#define BL_SDMMC_CLK        38
#define BL_SDMMC_CMD        34
#define BL_SDMMC_D0         39
#define BL_SDMMC_D1         40
#define BL_SDMMC_D2         47
#define BL_SDMMC_D3         33

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
