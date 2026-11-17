/*
 * bl_board.h - Generic ESP32-C5, 8 MB flash.
 * Any ESP32-C5 board with 8 MB of flash, e.g. ESP32-C5-DevKitC-1 N8R4; quad PSRAM found at runtime.
 * Console and uploads: the native USB port (on a DevKitC-1: the port labeled USB, not UART).
 * Pins: Arduino-ESP32's generic esp32c5 variant. LED_BUILTIN is the WS2812 on GPIO 27 of Espressif's ESP32-C5-DevKitC-1.
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once
#define BL_TARGET_NAME          "esp32c5_generic_8mb"
#define BL_BOARD_NAME           "Generic ESP32-C5, 8 MB flash"
/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    27
#define BL_BOARD_STAT_LED       27
#define BL_BOARD_RGB_LED        27           /* WS2812 */
#define BL_BOARD_SDA            0
#define BL_BOARD_SCL            1
#define BL_BOARD_A0             1
#define BL_BOARD_A1             2
#define BL_BOARD_A2             3
#define BL_BOARD_A3             4
#define BL_BOARD_A4             5
#define BL_BOARD_A5             6
#define BL_BOARD_SCK            10
#define BL_BOARD_MISO           9
#define BL_BOARD_MOSI           8
#define BL_BOARD_SS             6
/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           12
#define BL_SERIAL1_TX           11
#define BL_SERIAL2_RX           (-1)
#define BL_SERIAL2_TX           (-1)
#define BL_SD_DEFAULT_BUS       0           /* BL_SD_SPI */
#define BL_SD_CS                (-1)        /* no slot on the board: the sketch passes the chip select */
