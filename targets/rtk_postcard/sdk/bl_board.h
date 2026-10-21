/*
 * bl_board.h - SparkFun RTK Postcard.
 * ESP32-PICO-MINI-02 (ESP32-PICO-V3-02: 8 MB flash, 2 MB PSRAM), Quectel
 * LG290P quad-band GNSS, CH342 dual USB-serial (channel A: ESP32 console,
 * channel B: LG290P). Pins: docs.sparkfun.com/SparkFun_RTK_Postcard.
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches (LED_BUILTIN,
 * SDA, ...). #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "rtk_postcard"
#define BL_BOARD_NAME           "SparkFun RTK Postcard"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    0           /* green STAT LED (also the BOOT button) */
#define BL_BOARD_STAT_LED       0
#define BL_BOARD_SDA            13          /* the two Qwiic connectors */
#define BL_BOARD_SCL            19
#define BL_BOARD_A0             36          /* ADC1 channel 0 (also the PPS input, below) */
#define BL_BOARD_SCK            32
#define BL_BOARD_MISO           25          /* POCI */
#define BL_BOARD_MOSI           26          /* PICO */
#define BL_BOARD_SS             27

/* Board extras (sketches use these names directly). */
#define BL_BOARD_BT_LED         4           /* blue BT LED */
#define BL_BOARD_GNSS_TX        21          /* ESP32 TX -> LG290P UART2 RX (Serial1) */
#define BL_BOARD_GNSS_RX        22          /* ESP32 RX <- LG290P UART2 TX */
#define BL_BOARD_GNSS_BAUD      460800      /* LG290P UART2 as RTK Everywhere configures it */
#define BL_BOARD_GNSS_SDA       20          /* I2C bus shared with the LG290P */
#define BL_BOARD_GNSS_SCL       7
#define BL_BOARD_GNSS_RESET     33          /* LG290P RESET_N, active low */
#define BL_BOARD_GNSS_PPS       36          /* 1PPS (input only; also drives the PPS LED) */
#define BL_BOARD_GNSS_RTK_STAT  34          /* RTK status (input only; also drives the RTK LED) */
#define BL_BOARD_GNSS_EVENT     8           /* LG290P EVENT input */

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           22          /* Serial1 talks to the LG290P */
#define BL_SERIAL1_TX           21
#define BL_SERIAL2_RX           (-1)        /* -1: the sketch must pass pins */
#define BL_SERIAL2_TX           (-1)
#define BL_SD_DEFAULT_BUS       0           /* no microSD slot: SD.begin(cs) on SPI only */
#define BL_SD_CS                (-1)
