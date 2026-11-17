/*
 * bl_board.h - Generic ESP32-C3, 4 MB flash.
 * Any ESP32-C3 board with 4 MB of flash, e.g. C3 Super Mini, ESP32-C3-DevKitC-02; no PSRAM.
 * Console and uploads: the native USB port (boards with only a USB-serial chip, such as the ESP32-C3-DevKitM-1, are not supported).
 * Pins: Arduino-ESP32's generic esp32c3 variant. LED_BUILTIN is the WS2812 on GPIO 8 of Espressif's ESP32-C3 DevKits (a plain LED on many C3 Super Mini boards).
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once
#define BL_TARGET_NAME          "esp32c3_generic_4mb"
#define BL_BOARD_NAME           "Generic ESP32-C3, 4 MB flash"
/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    8
#define BL_BOARD_STAT_LED       8
#define BL_BOARD_RGB_LED        8           /* WS2812 */
#define BL_BOARD_SDA            8
#define BL_BOARD_SCL            9
#define BL_BOARD_A0             0
#define BL_BOARD_A1             1
#define BL_BOARD_A2             2
#define BL_BOARD_A3             3
#define BL_BOARD_A4             4
#define BL_BOARD_A5             5
#define BL_BOARD_SCK            4
#define BL_BOARD_MISO           5
#define BL_BOARD_MOSI           6
#define BL_BOARD_SS             7
/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           20
#define BL_SERIAL1_TX           21
#define BL_SERIAL2_RX           (-1)
#define BL_SERIAL2_TX           (-1)
#define BL_SD_DEFAULT_BUS       0           /* BL_SD_SPI */
#define BL_SD_CS                (-1)        /* no slot on the board: the sketch passes the chip select */
