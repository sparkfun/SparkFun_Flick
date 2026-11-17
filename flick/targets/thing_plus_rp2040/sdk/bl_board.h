/*
 * bl_board.h - SparkFun Thing Plus - RP2040.
 * RP2040, 16 MB flash, no PSRAM, native USB-C, Qwiic, microSD, WS2812, LiPo charger.
 * Pins: arduino-pico's sparkfun_thingplusrp2040 variant. Checked on hardware 2026-11-17
 * (flick test 27/0/3: microSD on SPI1, the fuel gauge at 0x36).
 * microSD on SPI1 (SCK 14, PICO 15, POCI 12, CS 9); the header's SPI is SPI0 (2/3/4).
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "thing_plus_rp2040"
#define BL_BOARD_NAME           "SparkFun Thing Plus - RP2040"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    25  /* blue STAT LED */
#define BL_BOARD_STAT_LED       25
#define BL_BOARD_RGB_LED        8   /* WS2812 */
#define BL_BOARD_SDA            6   /* Qwiic (I2C1) */
#define BL_BOARD_SCL            7
#define BL_BOARD_A0             26
#define BL_BOARD_A1             27
#define BL_BOARD_A2             28
#define BL_BOARD_A3             29
#define BL_BOARD_SCK            2
#define BL_BOARD_MISO           4   /* POCI */
#define BL_BOARD_MOSI           3   /* PICO */
#define BL_BOARD_SS             5

#define BL_BOARD_SD_CS          9           /* microSD on SPI1 */
#define BL_BOARD_FUEL_GAUGE_ADDR 0x36        /* MAX17048 LiPo fuel gauge on the Qwiic bus */

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           1           /* UART0 */
#define BL_SERIAL1_TX           0
#define BL_SERIAL2_RX           (-1)        /* -1: the sketch must pass pins */
#define BL_SERIAL2_TX           (-1)
#define BL_SD_DEFAULT_BUS       0           /* BL_SD_SPI */
#define BL_SD_CS                9
#define BL_SD_SCK               14          /* the microSD slot's own SPI pins (SPI1) */
#define BL_SD_MISO              12
#define BL_SD_MOSI              15

/* ---- Flash (XIP addresses; flick passes the first four to arduino-pico) ----
 * 0x10000000  Baseline, up to 4 MB
 * 0x10400000  LittleFS, 8 MB - 4 KB
 * 0x10BFF000  arduino-pico's EEPROM sector (Baseline's state: active slot, trial)
 * 0x10C00000  the sketch window: a copy of the active slot, 512 KB
 * 0x10C80000  sketch slot A, 512 KB
 * 0x10D00000  sketch slot B, 512 KB
 * 0x10D80000  free, 2.5 MB */
#define BL_BOARD_FLASH_SIZE     0x1000000
#define BL_BOARD_BASELINE_MAX   0x400000
#define BL_BOARD_FS_START       0x10400000
#define BL_BOARD_FS_END         0x10BFF000
#define BL_BOARD_EEPROM_START   0x10BFF000
#define BL_BOARD_SLOT_A         0x00C80000  /* flash offsets */
#define BL_BOARD_SLOT_B         0x00D00000
#define BL_BOARD_SLOT_SIZE      0x80000
