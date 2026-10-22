/*
 * bl_board.h - SparkFun DataLogger IoT, and DataLogger IoT - 9DoF.
 * ESP32-WROOM-32E, 16 MB flash, CH340 USB-serial, microSD (4-bit SDIO),
 * Qwiic, MAX17048 fuel gauge (0x36). Qwiic and microSD are powered from a
 * switched rail (GPIO 32 high = on). The 9DoF version adds an ISM330DHCX IMU
 * and an MMC5983MA magnetometer on SPI. Both have an addressable RGB LED on 26.
 * Pins: docs.sparkfun.com/SparkFun_DataLogger and SparkFun's firmware (sfe-datalogger).
 * Checked on hardware 2026-10-22 (DataLogger IoT, not the 9DoF).
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
#define BL_BOARD_RGB_LED        26          /* addressable RGB (WS2812 on the 9DoF, B3DQ3BRG on the base board) */
#define BL_BOARD_SDA            21          /* Qwiic */
#define BL_BOARD_SCL            22
#define BL_BOARD_A0             36
#define BL_BOARD_A3             39
/* A7 is GPIO 5 on the DataLogger IoT but GPIO 35 on the 9DoF (where 5 is the IMU
 * chip select), so this target leaves A7 undefined. */
#define BL_BOARD_SCK            18
#define BL_BOARD_MISO           19          /* POCI */
#define BL_BOARD_MOSI           23          /* PICO */
#define BL_BOARD_SS             5
#define BL_BOARD_IMU_CS         5           /* ISM330DHCX (9DoF version) */

#define BL_BOARD_EN_3V3_SW      32          /* switched 3.3 V rail for Qwiic and microSD: high = on (Baseline turns it on at boot) */
#define BL_BOARD_FUEL_GAUGE_ADDR 0x36        /* MAX17048 LiPo fuel gauge on the Qwiic bus */

/* ---- Baseline defaults ---- */
/* The RX/TX header. SparkFun's firmware (sfe-datalogger, sfeDLBoard.h) uses RX 16,
 * TX 17; the hookup guide lists them the other way round. Not yet checked. */
#define BL_SERIAL1_RX           16
#define BL_SERIAL1_TX           17
#define BL_SERIAL2_RX           (-1)
#define BL_SERIAL2_TX           (-1)
/* microSD: 4-bit SDIO on the classic ESP32's fixed SDMMC pins
 * (CLK 14, CMD 15, D0 2, D1 4, D2 12, D3 13). */
#define BL_SD_DEFAULT_BUS       1           /* BL_SD_MMC */
#define BL_SD_CS                (-1)
