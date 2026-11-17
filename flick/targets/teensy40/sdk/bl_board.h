/*
 * bl_board.h - PJRC Teensy 4.0.
 * i.MX RT1062 (Cortex-M7, 600 MHz), 2 MB QSPI flash, 1 MB RAM, native USB
 * (Teensy's own USB stack, VID 0x16C0). One plain LED on pin 13 (also SCK).
 * Pins: pjrc.com/teensy/pinout.html and Teensyduino's core_pins.h.
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "teensy40"
#define BL_BOARD_NAME           "PJRC Teensy 4.0"
#define BL_HAS_USB_AUDIO        1           /* USB type Serial + MIDI + Audio (the *_hid variant: keyboard instead) */

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
#define BL_SERIAL2_RX           7
#define BL_SERIAL2_TX           8

#define BL_BOARD_TEENSY_MODEL   0x24        /* Teensyduino's model ID (linker script) */

/* ---- Flash ----
 * 0x60000000  Baseline (up to 640 KB; its linker script checks)
 * 0x600A0000  sketch slot A, 384 KB
 * 0x60100000  sketch slot B, 384 KB
 * 0x60160000  LittleFS, 576 KB (Teensy's LittleFS_Program: the end of program flash)
 * 0x601F0000  Teensy's EEPROM emulation (60 KB) and the 4 KB restore program: never touched */
#define BL_BOARD_FLASH_SIZE     0x200000
#define BL_BOARD_SLOT_A         0x600A0000
#define BL_BOARD_SLOT_B         0x60100000
#define BL_BOARD_SLOT_SIZE      0x60000
#define BL_SKETCH_MAX_SIZE      0x60000     /* 384 KB: the sketch window, as big as a slot */
#define BL_BOARD_FS_SIZE        0x90000     /* LittleFS_Program.begin(): 18 blocks of 32 KB */
