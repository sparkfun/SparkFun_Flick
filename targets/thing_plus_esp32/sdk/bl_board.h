/*
 * bl_board.h - SparkFun Thing Plus - ESP32 WROOM (USB-C), the "Thing Plus C".
 * ESP32-WROOM-32E, 16 MB flash, no PSRAM, CH340 USB-serial.
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches (LED_BUILTIN,
 * SDA, ...). #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "thing_plus_esp32"
#define BL_BOARD_NAME           "SparkFun Thing Plus - ESP32 WROOM (USB-C)"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    13          /* blue STAT LED */
#define BL_BOARD_STAT_LED       13
#define BL_BOARD_RGB_LED        2           /* WS2812 */
#define BL_BOARD_SDA            21          /* Qwiic */
#define BL_BOARD_SCL            22
#define BL_BOARD_A0             26
#define BL_BOARD_SCK            18
#define BL_BOARD_MISO           19
#define BL_BOARD_MOSI           23
#define BL_BOARD_SS             5
#define BL_BOARD_SD_CS          5           /* microSD on SPI */

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           16          /* the pins labeled RX/TX */
#define BL_SERIAL1_TX           17
#define BL_SERIAL2_RX           (-1)        /* -1: the sketch must pass pins */
#define BL_SERIAL2_TX           (-1)
#define BL_SD_DEFAULT_BUS       0           /* BL_SD_SPI */
#define BL_SD_CS                5
