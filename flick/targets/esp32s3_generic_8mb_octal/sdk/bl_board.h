/*
 * bl_board.h - Generic ESP32-S3, 8 MB flash, octal PSRAM.
 * Any ESP32-S3 board with 8 MB of flash, e.g. N8R8; octal PSRAM.
 * Console and uploads: the native USB port (on a DevKitC-1: the port labeled USB, not UART).
 * Pins: Arduino-ESP32's generic esp32s3 variant. LED_BUILTIN is the WS2812 on GPIO 48 of Espressif's ESP32-S3-DevKitC-1 v1.0 (GPIO 38 on v1.1).
 * Octal PSRAM uses GPIO 33-37, so Baseline refuses those pins (bl_port.h).
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once
#define BL_TARGET_NAME          "esp32s3_generic_8mb_octal"
#define BL_BOARD_NAME           "Generic ESP32-S3, 8 MB flash, octal PSRAM"
/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    48
#define BL_BOARD_STAT_LED       48
#define BL_BOARD_RGB_LED        48           /* WS2812 */
#define BL_BOARD_SDA            8
#define BL_BOARD_SCL            9
#define BL_BOARD_A0             1
#define BL_BOARD_A1             2
#define BL_BOARD_A2             3
#define BL_BOARD_A3             4
#define BL_BOARD_A4             5
#define BL_BOARD_A5             6
#define BL_BOARD_SCK            12
#define BL_BOARD_MISO           13
#define BL_BOARD_MOSI           11
#define BL_BOARD_SS             10
/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           18
#define BL_SERIAL1_TX           17
#define BL_SERIAL2_RX           (-1)
#define BL_SERIAL2_TX           (-1)
#define BL_SD_DEFAULT_BUS       0           /* BL_SD_SPI */
#define BL_SD_CS                (-1)        /* no slot on the board: the sketch passes the chip select */
