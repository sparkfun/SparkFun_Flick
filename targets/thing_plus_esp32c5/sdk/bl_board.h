/*
 * bl_board.h - SparkFun Thing Plus - ESP32-C5.
 * ESP32-C5 (RISC-V, single core), 8 MB flash, 8 MB PSRAM, dual-band WiFi 6, native USB-C, Qwiic.
 * Pins: Arduino-ESP32's sparkfun_esp32c5_thing_plus variant.
 * NOT YET CHECKED ON HARDWARE.
 * The only LED is the WS2812 on GPIO 27, so LED_BUILTIN points at it.
 * The low-power I2C pins (LP_SDA 2, LP_SCL 3) are not used by Baseline.
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "thing_plus_esp32c5"
#define BL_BOARD_NAME           "SparkFun Thing Plus - ESP32-C5"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    27  /* the WS2812 (no plain LED) */
#define BL_BOARD_STAT_LED       27
#define BL_BOARD_RGB_LED        27  /* WS2812 */
#define BL_BOARD_SDA            23  /* Qwiic */
#define BL_BOARD_SCL            24
#define BL_BOARD_A0             1
#define BL_BOARD_SCK            10
#define BL_BOARD_MISO           9   /* POCI */
#define BL_BOARD_MOSI           8   /* PICO */
#define BL_BOARD_SS             25

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           12          /* the pins labeled RX/TX */
#define BL_SERIAL1_TX           11
#define BL_SERIAL2_RX           (-1)        /* -1: the sketch must pass pins */
#define BL_SERIAL2_TX           (-1)
#define BL_SD_DEFAULT_BUS       0           /* BL_SD_SPI (no SDMMC host on this chip) */
#define BL_SD_CS                (-1)        /* the sketch passes the chip select */
