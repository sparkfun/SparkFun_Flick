/*
 * bl_board.h - SparkFun Thing Plus - ESP32-C5.
 * ESP32-C5 (RISC-V, single core), 8 MB flash, 8 MB PSRAM, dual-band WiFi 6, native USB-C, Qwiic.
 * Pins: Arduino-ESP32's sparkfun_esp32c5_thing_plus variant and SparkFun's
 * schematic (SparkFun_Thing_Plus_ESP32_C5.pdf).
 * NOT YET CHECKED ON HARDWARE.
 * The only LED is the WS2812 on GPIO 27, so LED_BUILTIN points at it.
 * The low-power I2C pins (LP_SDA 2, LP_SCL 3) are not used by Baseline.
 * microSD on SPI: CS 25 (SCK 10, PICO 8, POCI 9), card detect 7 (SD_DET jumper).
 * MAX17048 fuel gauge alert on GPIO 0 (ALRT jumper).
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
#define BL_BOARD_A1             2
#define BL_BOARD_A2             3
#define BL_BOARD_A3             4
#define BL_BOARD_A4             5
#define BL_BOARD_A5             6
#define BL_BOARD_SCK            10
#define BL_BOARD_MISO           9   /* POCI */
#define BL_BOARD_MOSI           8   /* PICO */
#define BL_BOARD_SS             25

#define BL_BOARD_SD_CS          25          /* microSD on SPI */
#define BL_BOARD_SD_DETECT      7           /* card detect (SD_DET jumper) */
#define BL_BOARD_FUEL_GAUGE_ADDR 0x36        /* MAX17048 LiPo fuel gauge on the Qwiic bus */
#define BL_BOARD_FUEL_GAUGE_ALERT 0          /* (ALRT jumper) */

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           12          /* the pins labeled RX/TX */
#define BL_SERIAL1_TX           11
#define BL_SERIAL2_RX           (-1)        /* -1: the sketch must pass pins */
#define BL_SERIAL2_TX           (-1)
#define BL_SD_DEFAULT_BUS       0           /* BL_SD_SPI (no SDMMC host on this chip) */
#define BL_SD_CS                25
