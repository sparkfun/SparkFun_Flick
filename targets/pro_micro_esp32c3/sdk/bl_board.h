/*
 * bl_board.h - SparkFun Pro Micro - ESP32-C3.
 * ESP32-C3 (RISC-V, single core, no PSRAM), 4 MB flash, native USB-C, Qwiic.
 * Pins: Arduino-ESP32's sparkfun_pro_micro_esp32c3 variant.
 * NOT YET CHECKED ON HARDWARE.
 * Check whether the LED on GPIO 10 is a plain LED or a WS2812 (if WS2812,
 * add BL_BOARD_RGB_LED 10).
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "pro_micro_esp32c3"
#define BL_BOARD_NAME           "SparkFun Pro Micro - ESP32-C3"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    10  /* STAT LED (also SS) */
#define BL_BOARD_STAT_LED       10
#define BL_BOARD_SDA            5   /* Qwiic */
#define BL_BOARD_SCL            6
#define BL_BOARD_A0             0
#define BL_BOARD_A1             1
#define BL_BOARD_A2             2
#define BL_BOARD_A3             3
#define BL_BOARD_A4             4
#define BL_BOARD_SCK            0
#define BL_BOARD_MISO           1   /* POCI */
#define BL_BOARD_MOSI           3   /* PICO */
#define BL_BOARD_SS             10

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           20          /* the pins labeled RX/TX */
#define BL_SERIAL1_TX           21
#define BL_SERIAL2_RX           (-1)        /* -1: the sketch must pass pins */
#define BL_SERIAL2_TX           (-1)
#define BL_SD_DEFAULT_BUS       0           /* BL_SD_SPI (no SDMMC host on this chip) */
#define BL_SD_CS                (-1)        /* the sketch passes the chip select */
