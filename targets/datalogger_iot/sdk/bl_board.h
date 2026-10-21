/*
 * bl_board.h - SparkFun DataLogger IoT, and DataLogger IoT - 9DoF.
 * ESP32-WROOM-32E, 16 MB flash, CH340 USB-serial, microSD (4-bit SDIO),
 * Qwiic, MAX17048 fuel gauge (0x36). The 9DoF version adds an ISM330DHCX IMU
 * and an MMC5983MA magnetometer on SPI, and a WS2812 RGB LED.
 * Pins: docs.sparkfun.com/SparkFun_DataLogger. NOT YET CHECKED ON HARDWARE.
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "datalogger_iot"
#define BL_BOARD_NAME           "SparkFun DataLogger IoT"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    25          /* STAT LED */
#define BL_BOARD_STAT_LED       25
#define BL_BOARD_RGB_LED        26          /* WS2812 on the 9DoF version */
#define BL_BOARD_SDA            21          /* Qwiic */
#define BL_BOARD_SCL            22
#define BL_BOARD_A0             36
#define BL_BOARD_SCK            18
#define BL_BOARD_MISO           19          /* POCI */
#define BL_BOARD_MOSI           23          /* PICO */
#define BL_BOARD_SS             5
#define BL_BOARD_IMU_CS         5           /* ISM330DHCX (9DoF version) */

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           17          /* the RX/TX header */
#define BL_SERIAL1_TX           16
#define BL_SERIAL2_RX           (-1)
#define BL_SERIAL2_TX           (-1)
/* microSD: 4-bit SDIO on the classic ESP32's fixed SDMMC pins
 * (CLK 14, CMD 15, D0 2, D1 4, D2 12, D3 13). */
#define BL_SD_DEFAULT_BUS       1           /* BL_SD_MMC */
#define BL_SD_CS                (-1)
