/*
 * bl_board.h - SparkFun MicroMod Teensy Processor (Teensyduino's "Teensy MicroMod").
 * i.MX RT1062 (Cortex-M7, 600 MHz), 16 MB QSPI flash, 1 MB RAM, no PSRAM,
 * native USB (Teensy's own stack, VID 0x16C0), on a MicroMod carrier board.
 * One plain LED on pin 13 (also SCK). Pins are the Teensy numbers of
 * Teensyduino's core (core_pins.h, pins_arduino.h); a carrier board labels
 * them by MicroMod name (I2C_SDA = 18, I2C_SCL = 19, ...).
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "teensy_micromod"
#define BL_BOARD_NAME           "SparkFun MicroMod Teensy"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    13
#define BL_BOARD_STAT_LED       13
#define BL_BOARD_SDA            18  /* Wire */
#define BL_BOARD_SCL            19
#define BL_BOARD_A0             14
#define BL_BOARD_A1             15
#define BL_BOARD_A2             16
#define BL_BOARD_A3             17
#define BL_BOARD_A4             18
#define BL_BOARD_A5             19
#define BL_BOARD_A6             20
#define BL_BOARD_A7             21
#define BL_BOARD_SCK            13
#define BL_BOARD_MISO           12
#define BL_BOARD_MOSI           11
#define BL_BOARD_SS             10

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           0
#define BL_SERIAL1_TX           1
#define BL_SERIAL2_RX           16          /* MicroMod UART2: LPUART3, not the 4.x pins 7/8 */
#define BL_SERIAL2_TX           17

#define BL_BOARD_TEENSY_MODEL   0x26        /* Teensyduino's model ID (linker script) */

/* ---- Flash ----
 * 0x60000000  Baseline (up to 2 MB; its linker script checks)
 * 0x60200000  sketch slot A, 1 MB
 * 0x60300000  sketch slot B, 1 MB
 * 0x60400000  LittleFS, 11.75 MB (Teensy's LittleFS_Program: the end of program flash)
 * 0x60FC0000  Teensy's EEPROM emulation (252 KB) and the 4 KB restore program: never touched */
#define BL_BOARD_FLASH_SIZE     0x1000000
#define BL_BOARD_SLOT_A         0x60200000
#define BL_BOARD_SLOT_B         0x60300000
#define BL_BOARD_SLOT_SIZE      0x100000
#define BL_SKETCH_MAX_SIZE      0x100000    /* 1 MB: the sketch window, as big as a slot */
#define BL_BOARD_FS_SIZE        0xBC0000    /* LittleFS_Program.begin(): 188 blocks of 64 KB */
