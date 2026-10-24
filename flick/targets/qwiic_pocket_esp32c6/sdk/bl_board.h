/*
 * bl_board.h - SparkFun Qwiic Pocket Development Board - ESP32-C6.
 * ESP32-C6 (RISC-V, single core, no PSRAM), 4 MB flash, native USB-C, Qwiic.
 * Pins: Arduino-ESP32's sparkfun_esp32c6_qwiic_pocket variant.
 * Pins also from docs.sparkfun.com/SparkFun_Qwiic_Pocket_Dev_Board_ESP32_C6:
 * STAT LED 23, Qwiic 6/7, battery voltage on GPIO 1 (divider x1.688), no fuel gauge.
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "qwiic_pocket_esp32c6"
#define BL_BOARD_NAME           "SparkFun Qwiic Pocket Development Board - ESP32-C6"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    23  /* STAT LED */
#define BL_BOARD_STAT_LED       23
#define BL_BOARD_SDA            6   /* Qwiic */
#define BL_BOARD_SCL            7
#define BL_BOARD_A0             0
#define BL_BOARD_A1             1
#define BL_BOARD_A2             2
#define BL_BOARD_A3             3
#define BL_BOARD_A4             4
#define BL_BOARD_A5             5
#define BL_BOARD_A6             6
#define BL_BOARD_SCK            5
#define BL_BOARD_MISO           4   /* POCI */
#define BL_BOARD_MOSI           3   /* PICO */
#define BL_BOARD_SS             2

#define BL_BOARD_VBAT_ADC       1           /* LiPo voltage through a divider (MCP73831 charger) */
#define BL_BOARD_VBAT_SCALE     1.688f      /* battery mV = analogReadMilliVolts(VBAT) x this */

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           17          /* the pins labeled RX/TX */
#define BL_SERIAL1_TX           16
#define BL_SERIAL2_RX           (-1)        /* -1: the sketch must pass pins */
#define BL_SERIAL2_TX           (-1)
#define BL_SD_DEFAULT_BUS       0           /* BL_SD_SPI (no SDMMC host on this chip) */
#define BL_SD_CS                (-1)        /* the sketch passes the chip select */
