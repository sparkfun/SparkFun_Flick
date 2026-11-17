/*
 * bl_board.h - Generic ESP32, 4 MB flash.
 * Any ESP32 board with 4 MB of flash, e.g. ESP32-WROOM-32 DevKits; PSRAM found at runtime.
 * Console and uploads: a USB-serial chip (CP2102, CH340, ...).
 * Pins: Arduino-ESP32's generic esp32 variant. LED_BUILTIN is GPIO 2: the blue LED on most ESP32 DevKit clones (Espressif's DevKitC has none).
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once
#define BL_TARGET_NAME          "esp32_generic_4mb"
#define BL_BOARD_NAME           "Generic ESP32, 4 MB flash"
/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    2
#define BL_BOARD_STAT_LED       2
#define BL_BOARD_SDA            21
#define BL_BOARD_SCL            22
#define BL_BOARD_A0             36
#define BL_BOARD_A3             39
#define BL_BOARD_A4             32
#define BL_BOARD_A5             33
#define BL_BOARD_A6             34
#define BL_BOARD_A7             35
#define BL_BOARD_SCK            18
#define BL_BOARD_MISO           19
#define BL_BOARD_MOSI           23
#define BL_BOARD_SS             5
/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           (-1)
#define BL_SERIAL1_TX           (-1)
#define BL_SERIAL2_RX           16
#define BL_SERIAL2_TX           17
#define BL_SD_DEFAULT_BUS       0           /* BL_SD_SPI */
#define BL_SD_CS                (-1)        /* no slot on the board: the sketch passes the chip select */
