/*
 * bl_board.h - SparkFun RedBoard Artemis ATP.
 * Artemis module (Apollo3 Blue), 1 MB flash, 384 KB RAM, CH340 USB serial on
 * UART0 (pads 48/49), Qwiic, every pad on a header.
 * Pins: the Apollo3 core's SFE_ARTEMIS_ATP variant and BSP. Pin numbers are
 * pad numbers, as printed on the board.
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "redboard_artemis_atp"
#define BL_BOARD_NAME           "SparkFun RedBoard Artemis ATP"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    5   /* blue STAT LED, shared with SPI SCK (as on an Uno) */
#define BL_BOARD_STAT_LED       5
#define BL_BOARD_SDA            40  /* Qwiic (IOM4) */
#define BL_BOARD_SCL            39
#define BL_BOARD_SCK            5   /* IOM0 */
#define BL_BOARD_MISO           6   /* POCI */
#define BL_BOARD_MOSI           7   /* PICO */
#define BL_BOARD_SS             11
/* A0-A5: the analog pins in the Uno's A0-A5 positions, as printed there
 * (A29 A11 A34 A33 A16 A31); A6/A7: the other two ADC pads (A32, A35).
 * analogRead() takes any ADC pad by number too. */
#define BL_BOARD_A0             29
#define BL_BOARD_A1             11
#define BL_BOARD_A2             34
#define BL_BOARD_A3             33
#define BL_BOARD_A4             16
#define BL_BOARD_A5             31
#define BL_BOARD_A6             32
#define BL_BOARD_A7             35

/* 04_analog_read reads this pin: the last analog pin on the bottom header. */
#define BL_BOARD_ANALOG_DEMO_PIN  31
#define BL_BOARD_ANALOG_DEMO_NAME "A31"

/* LittleFS: the region bl_target.h gives it (112 KB of the 1 MB flash). */
#define BL_BOARD_FS_SIZE        (BL_APOLLO3_FS_END - BL_APOLLO3_FS_START)

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           25          /* UART1: the header pins printed RX1 / TX1 (pads 25 / 24) */
#define BL_SERIAL1_TX           24
#define BL_SERIAL2_RX           (-1)        /* the Apollo3 has two UARTs: UART0 is the console */
#define BL_SERIAL2_TX           (-1)
