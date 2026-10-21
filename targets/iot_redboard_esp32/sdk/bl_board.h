/*
 * bl_board.h - SparkFun IoT RedBoard - ESP32 (Arduino R3 form factor).
 * Classic ESP32, 4 MB flash, CH340G USB-serial, microSD, Qwiic, MAX17048.
 * Pins: Arduino-ESP32's sparkfun_esp32_iot_redboard variant and the SparkFun
 * hookup guide. NOT YET CHECKED ON HARDWARE, nor that the chip is rev 3.0+
 * (the hookup guide names an ESP32-D0WDQ6; Baseline needs rev 3.0 or later).
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "iot_redboard_esp32"
#define BL_BOARD_NAME           "SparkFun IoT RedBoard - ESP32"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    18          /* STAT LED, shared with SCK */
#define BL_BOARD_STAT_LED       18
#define BL_BOARD_RGB_LED        2           /* WS2812 */
#define BL_BOARD_SDA            21          /* Qwiic */
#define BL_BOARD_SCL            22
#define BL_BOARD_A0             36
#define BL_BOARD_SCK            18
#define BL_BOARD_MISO           19
#define BL_BOARD_MOSI           23
#define BL_BOARD_SS             5
#define BL_BOARD_SD_CS          5           /* microSD on SPI (assumed SS; check the schematic) */
#define BL_BOARD_FUEL_GAUGE_ALERT 4         /* MAX17048 ALERT, when its jumper is closed */

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           (-1)        /* RX/TX on the header are UART0, the console */
#define BL_SERIAL1_TX           (-1)
#define BL_SERIAL2_RX           (-1)
#define BL_SERIAL2_TX           (-1)
#define BL_SD_DEFAULT_BUS       0           /* BL_SD_SPI */
#define BL_SD_CS                5
