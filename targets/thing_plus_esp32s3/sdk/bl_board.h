/*
 * bl_board.h - SparkFun Thing Plus - ESP32-S3.
 * ESP32-S3-MINI-1, 4 MB flash, 2 MB quad PSRAM, native USB.
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches (LED_BUILTIN,
 * SDA, ...). #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "thing_plus_esp32s3"
#define BL_BOARD_NAME           "SparkFun Thing Plus - ESP32-S3"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    0           /* blue STAT LED */
#define BL_BOARD_STAT_LED       0
#define BL_BOARD_RGB_LED        46          /* WS2812 */
#define BL_BOARD_SDA            8           /* Qwiic */
#define BL_BOARD_SCL            9
#define BL_BOARD_A0             10
#define BL_BOARD_SCK            12
#define BL_BOARD_MISO           13
#define BL_BOARD_MOSI           11
#define BL_BOARD_SS             10

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           44          /* the pins labeled RX/TX */
#define BL_SERIAL1_TX           43
#define BL_SERIAL2_RX           (-1)        /* -1: the sketch must pass pins */
#define BL_SERIAL2_TX           (-1)
/* microSD: wired as 4-bit SDIO (SD_MMC), not SPI. */
#define BL_SD_DEFAULT_BUS       1           /* BL_SD_MMC */
#define BL_SD_CS                (-1)
#define BL_SDMMC_CLK            38
#define BL_SDMMC_CMD            34
#define BL_SDMMC_D0             39
#define BL_SDMMC_D1             40
#define BL_SDMMC_D2             47
#define BL_SDMMC_D3             33
