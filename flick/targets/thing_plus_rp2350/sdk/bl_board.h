/*
 * bl_board.h - SparkFun Thing Plus - RP2350.
 * RP2350A (Arm cores), 16 MB flash, 8 MB PSRAM (CS on GPIO 8), RM2 radio module
 * (Infineon CYW43439: WiFi 4 and Bluetooth 5.2, on PIO SPI), native USB-C,
 * Qwiic, microSD, WS2812, LiPo charger.
 * Pins: arduino-pico's sparkfun_thingplusrp2350 variant and
 * docs.sparkfun.com/SparkFun_Thing_Plus_RP2350; checked on hardware 2026-11-17 (flick test).
 * LED_BUILTIN is the blue STAT LED on the RM2's GPIO 0 (arduino-pico's pin 64):
 * arduino-pico starts the radio driver at boot, and digitalWrite reaches it
 * through the driver (seen blinking 2026-11-17).
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "thing_plus_rp2350"
#define BL_BOARD_NAME           "SparkFun Thing Plus - RP2350"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    64  /* STAT LED on the radio (CYW43 GPIO 0) */
#define BL_BOARD_STAT_LED       64
#define BL_BOARD_RGB_LED        14  /* WS2812 */
#define BL_BOARD_SDA            6   /* Qwiic (I2C1) */
#define BL_BOARD_SCL            7
#define BL_BOARD_A0             26
#define BL_BOARD_A1             27
#define BL_BOARD_A2             28
#define BL_BOARD_SCK            2
#define BL_BOARD_MISO           4   /* POCI */
#define BL_BOARD_MOSI           3   /* PICO */
#define BL_BOARD_SS             9

#define BL_BOARD_SD_CS          9           /* microSD on SPI0 (no card detect) */
#define BL_BOARD_FUEL_GAUGE_ADDR 0x36        /* MAX17048 LiPo fuel gauge on the Qwiic bus */
#define BL_BOARD_EN_3V3_SW      13          /* EN of the 3.3V_P rail (Qwiic, microSD, RGB LED): high = on. The LP
                                               jumper keeps it on by default; Baseline drives it high at boot too */

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           1           /* UART0 */
#define BL_SERIAL1_TX           0
#define BL_SERIAL2_RX           (-1)        /* -1: the sketch must pass pins */
#define BL_SERIAL2_TX           (-1)
#define BL_SD_DEFAULT_BUS       0           /* BL_SD_SPI */
#define BL_SD_CS                9
#define BL_BOARD_PSRAM_CS       8

/* ---- Features of this board (the chip has no radio of its own: the RM2, a CYW43439) ---- */
#define BL_HAS_WIFI             1       /* WiFi, HTTP(S), web server, MQTT, mDNS, WiFi uploads */
#define BL_HAS_BLE              1       /* Bluetooth LE: BLE serial, GATT server and client, scanning (BTstack) */

/* ---- Flash (XIP addresses; flick passes the first four to arduino-pico) ----
 * 0x10000000  Baseline, up to 4 MB (with the radio's firmware)
 * 0x10400000  LittleFS, 8 MB - 4 KB
 * 0x10BFF000  arduino-pico's EEPROM sector (Baseline's state: active slot, trial)
 * 0x10C00000  sketch slot A, 512 KB  } the last 4 MB: reached only through the
 * 0x10C80000  sketch slot B, 512 KB  } sketch window (ATRANS3), never directly
 * 0x10D00000  free, 3 MB */
#define BL_BOARD_FLASH_SIZE     0x1000000
#define BL_BOARD_BASELINE_MAX   0x400000
#define BL_BOARD_FS_START       0x10400000
#define BL_BOARD_FS_END         0x10BFF000
#define BL_BOARD_EEPROM_START   0x10BFF000
#define BL_BOARD_SLOT_A         0x00C00000  /* flash offsets */
#define BL_BOARD_SLOT_B         0x00C80000
#define BL_BOARD_SLOT_SIZE      0x80000
