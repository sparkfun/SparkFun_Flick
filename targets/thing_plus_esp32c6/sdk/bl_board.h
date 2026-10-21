/*
 * bl_board.h - SparkFun Thing Plus - ESP32-C6.
 * ESP32-C6 (RISC-V, single core, no PSRAM), native USB-C, Qwiic, microSD.
 * Pins: Arduino-ESP32's sparkfun_esp32c6_thing_plus variant.
 * NOT YET CHECKED ON HARDWARE.
 * Flash: 16 MB (esptool, 2026-10-21; Arduino's board file says 4 MB).
 * The only LED is the WS2812 on GPIO 23, so LED_BUILTIN points at it.
 * microSD chip select: unknown, pass it to SD.begin(cs) for now.
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "thing_plus_esp32c6"
#define BL_BOARD_NAME           "SparkFun Thing Plus - ESP32-C6"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    23  /* the WS2812 (no plain LED) */
#define BL_BOARD_STAT_LED       23
#define BL_BOARD_RGB_LED        23  /* WS2812 */
#define BL_BOARD_SDA            6   /* Qwiic */
#define BL_BOARD_SCL            7
#define BL_BOARD_A0             0
#define BL_BOARD_SCK            19
#define BL_BOARD_MISO           21  /* POCI */
#define BL_BOARD_MOSI           20  /* PICO */
#define BL_BOARD_SS             5

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           17          /* the pins labeled RX/TX */
#define BL_SERIAL1_TX           16
#define BL_SERIAL2_RX           (-1)        /* -1: the sketch must pass pins */
#define BL_SERIAL2_TX           (-1)
#define BL_SD_DEFAULT_BUS       0           /* BL_SD_SPI (no SDMMC host on this chip) */
#define BL_SD_CS                (-1)        /* the sketch passes the chip select */
